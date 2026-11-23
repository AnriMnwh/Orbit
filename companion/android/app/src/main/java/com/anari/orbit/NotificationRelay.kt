package com.anari.orbit

import android.app.KeyguardManager
import android.app.Notification
import android.app.PendingIntent
import android.app.RemoteInput
import android.content.Intent
import android.os.Bundle
import android.os.PowerManager
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.anari.orbit.link.Link
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/**
 * Forwards the phone's notifications and incoming calls to the watch, and
 * carries out what the watch asks: dismiss, quick reply, answer, decline.
 *
 * Android starts this itself once the user grants notification access.
 */
class NotificationRelay : NotificationListenerService() {
    /** The watch names a notification by number; Android names it by key. */
    private val idByKey = ConcurrentHashMap<String, Int>()
    private val keyById = ConcurrentHashMap<Int, String>()
    private val lastText = ConcurrentHashMap<String, String>()
    private val nextId = AtomicInteger(1)

    @Volatile private var ringingKey: String? = null
    private val settings by lazy { Settings(this) }

    override fun onListenerConnected() {
        Hub.notifications = this
        Hub.service?.onNotificationAccess()
    }

    override fun onListenerDisconnected() {
        Hub.notifications = null
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val service = Hub.service ?: return
        val notification = sbn.notification
        val extras = notification.extras

        if (notification.category == Notification.CATEGORY_CALL) {
            // 1 is Android's code for an incoming call that is still ringing.
            val type = extras.getInt(EXTRA_CALL_TYPE, 0)
            val ringing = type == 1 || (type == 0 && notification.fullScreenIntent != null)
            if (ringing) {
                ringingKey = sbn.key
                val who = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
                service.send(JSONObject().put("t", "call").put("cmd", "incoming").put("name", Link.clip(who, 39)))
            } else if (sbn.key == ringingKey) {
                ringingKey = null
                service.send(JSONObject().put("t", "call").put("cmd", "start"))
            }
            return
        }

        if (!worthShowing(sbn)) return
        // Remember the app so the Alerts tab can offer a switch for it, then apply the user's choices.
        val app = appName(sbn.packageName)
        settings.noteApp(sbn.packageName, app)
        if (sbn.packageName in settings.blockedApps) return
        if (settings.quietWhileUsingPhone && phoneInUse()) return
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty().trim()
        val body = (extras.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: extras.getCharSequence(Notification.EXTRA_TEXT))
            ?.toString().orEmpty().trim()
        if (title.isEmpty() && body.isEmpty()) return
        // Apps often post the same notification again; the watch should buzz once.
        if (lastText.put(sbn.key, "$title\n$body") == "$title\n$body") return

        val id = idByKey.getOrPut(sbn.key) { nextId.getAndIncrement().also { keyById[it] = sbn.key } }
        // Field lengths match what the watch keeps (main/core/orbit.h).
        service.send(
            JSONObject().put("t", "notify").put("id", id)
                .put("src", Link.clip(app, 23))
                .put("title", Link.clip(title, 47))
                .put("body", Link.clip(body, 159))
                .put("reply", replyAction(notification) != null),
        )
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.key == ringingKey) {
            ringingKey = null
            Hub.service?.send(JSONObject().put("t", "call").put("cmd", "end"))
        }
        lastText.remove(sbn.key)
        val id = idByKey.remove(sbn.key) ?: return
        keyById.remove(id)
        Hub.service?.send(JSONObject().put("t", "notify-").put("id", id))
    }

    /** Leaves out what would only be noise on a wrist. */
    private fun worthShowing(sbn: StatusBarNotification): Boolean {
        val n = sbn.notification
        if (sbn.packageName == packageName || sbn.isOngoing) return false
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return false
        if (n.category == Notification.CATEGORY_TRANSPORT || n.category == Notification.CATEGORY_SERVICE) return false
        // Respect Do Not Disturb: skip what the phone itself is keeping quiet.
        val ranking = Ranking()
        if (currentRanking?.getRanking(sbn.key, ranking) == true && !ranking.matchesInterruptionFilter()) return false
        return true
    }

    /** Unlocked with the screen on: the user will see the notification on the phone itself. */
    private fun phoneInUse(): Boolean =
        getSystemService(PowerManager::class.java).isInteractive && !getSystemService(KeyguardManager::class.java).isKeyguardLocked

    private fun appName(packageName: String): String = try {
        packageManager.getApplicationLabel(packageManager.getApplicationInfo(packageName, 0)).toString()
    } catch (e: Exception) {
        packageName.substringAfterLast('.')
    }

    private fun replyAction(notification: Notification): Notification.Action? =
        notification.actions?.firstOrNull { action -> action.remoteInputs?.any { it.allowFreeFormInput } == true }

    // ---------------------------------------------------------- from the watch

    /** {"t":"notify","n":"DISMISS","id":7} or {"t":"notify","n":"REPLY","id":7,"msg":"On my way"}. */
    fun fromWatch(json: JSONObject) {
        val key = keyById[json.optInt("id")] ?: return
        when (json.optString("n")) {
            "DISMISS" -> cancelNotification(key)
            "REPLY" -> reply(key, json.optString("msg"))
        }
    }

    private fun reply(key: String, text: String) {
        if (text.isBlank()) return
        val notification = activeNotifications?.firstOrNull { it.key == key }?.notification ?: return
        val action = replyAction(notification) ?: return
        val inputs = action.remoteInputs ?: return
        val results = Bundle()
        for (input in inputs) results.putCharSequence(input.resultKey, text)
        val fill = Intent()
        RemoteInput.addResultsToIntent(inputs, fill, results)
        send(action.actionIntent, fill)
    }

    /** Presses the phone app's own Answer or Decline button, so no phone permission is needed. */
    fun answerCall(accept: Boolean) {
        val key = ringingKey ?: return
        val notification = activeNotifications?.firstOrNull { it.key == key }?.notification ?: return
        val named = notification.extras.getParcelable(if (accept) EXTRA_ANSWER_INTENT else EXTRA_DECLINE_INTENT, PendingIntent::class.java)
        val wanted = if (accept) ANSWER else DECLINE
        val button = notification.actions?.firstOrNull { wanted.containsMatchIn(it.title ?: "") }?.actionIntent
        send(named ?: button ?: return, null)
    }

    private fun send(intent: PendingIntent?, fill: Intent?) {
        try {
            intent?.send(this, 0, fill)
        } catch (e: PendingIntent.CanceledException) {
            Log.i(TAG, "that notification's button is no longer valid")
        }
    }

    private companion object {
        const val TAG = "OrbitNotify"
        const val EXTRA_CALL_TYPE = "android.callType"
        const val EXTRA_ANSWER_INTENT = "android.answerIntent"
        const val EXTRA_DECLINE_INTENT = "android.declineIntent"
        val ANSWER = Regex("answer|accept", RegexOption.IGNORE_CASE)
        val DECLINE = Regex("decline|reject|dismiss|hang", RegexOption.IGNORE_CASE)
    }
}
