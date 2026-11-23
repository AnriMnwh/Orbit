package com.anari.orbit

import android.content.Context
import android.graphics.Bitmap
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import androidx.core.content.edit
import com.anari.orbit.link.BatteryCharge
import com.anari.orbit.link.VisionChoice
import com.anari.orbit.link.WatchConfig
import kotlinx.coroutines.flow.MutableStateFlow
import org.json.JSONObject
import java.security.KeyStore
import java.time.LocalDate
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** What the setup screen shows. Written by the services, read by the screen. */
data class WatchState(
    val address: String? = null,
    val connected: Boolean = false,
    /** The watch answered the app's hello, so the extras (art, assistant) are on. */
    val orbitLink: Boolean = false,
    val firmware: String = "",
    val battery: Int = -1,
    val charging: Boolean = false,
    val track: String = "",
    val artist: String = "",
    val art: Bitmap? = null,
    val assistant: String = "",
    /** The extras this watch's software supports, from its hello. Empty for older software. */
    val caps: Set<String> = emptySet(),
    /** The watch's own settings; null until it reports them. */
    val config: WatchConfig? = null,
    val weather: String = "",
    val nextEvent: String = "",
    /** Steps for the last seven days, oldest first. */
    val steps: List<Pair<LocalDate, Int>> = emptyList(),
    /** The watch's reason for not taking more photos just now, or empty. */
    val photoProblem: String = "",
    /** The gem on the watch's Vision face; null until the watch reports it. */
    val vision: VisionChoice? = null,
    /** The watch's Battery Charge tracker; null until the watch reports it. */
    val batteryCharge: BatteryCharge? = null,
)

/** The one place the app's parts find each other. They all live in one process. */
object Hub {
    val state = MutableStateFlow(WatchState())

    @Volatile var service: LinkService? = null
    @Volatile var notifications: NotificationRelay? = null
}

/** Saved choices. The OpenAI key is stored encrypted with a key that never leaves this phone. */
class Settings(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences("orbit", Context.MODE_PRIVATE)

    var watchAddress: String?
        get() = prefs.getString("watch_address", null)
        set(value) = prefs.edit { putString("watch_address", value) }

    var openAiKey: String?
        get() = prefs.getString("ai_key", null)?.let(Secrets::open)
        set(value) = prefs.edit { putString("ai_key", value?.takeIf { it.isNotBlank() }?.let(Secrets::seal)) }

    // The same defaults as the watch's own assistant (main/platform/agent.c). OpenAI
    // retires model names from time to time, so they are settings, not constants.
    var sttModel: String
        get() = prefs.getString("ai_stt", null) ?: "gpt-transcribe"
        set(value) = prefs.edit { putString("ai_stt", value.trim().ifEmpty { null }) }

    var chatModel: String
        get() = prefs.getString("ai_chat", null) ?: "gpt-6-luna"
        set(value) = prefs.edit { putString("ai_chat", value.trim().ifEmpty { null }) }

    var ttsModel: String
        get() = prefs.getString("ai_tts", null) ?: "gpt-4o-mini-tts-2025-12-15"
        set(value) = prefs.edit { putString("ai_tts", value.trim().ifEmpty { null }) }

    var voice: String
        get() = prefs.getString("ai_voice", null) ?: "marin"
        set(value) = prefs.edit { putString("ai_voice", value.trim().ifEmpty { null }) }

    // ---- notifications

    /** Apps whose notifications stay on the phone. */
    var blockedApps: Set<String>
        get() = prefs.getStringSet("blocked_apps", emptySet()).orEmpty()
        set(value) = prefs.edit { putStringSet("blocked_apps", value) }

    /** Keep the wrist quiet while the phone is unlocked and in use. */
    var quietWhileUsingPhone: Boolean
        get() = prefs.getBoolean("quiet_in_use", false)
        set(value) = prefs.edit { putBoolean("quiet_in_use", value) }

    var quickReplies: List<String>
        get() = prefs.getString("quick_replies", null)?.split('\n')?.filter { it.isNotBlank() } ?: DEFAULT_REPLIES
        set(value) = prefs.edit { putString("quick_replies", value.joinToString("\n")) }

    /** Apps that have sent a notification, with their names, so the filter list has something to show. */
    fun seenApps(): Map<String, String> = try {
        val json = JSONObject(prefs.getString("seen_apps", null) ?: "{}")
        json.keys().asSequence().associateWith { json.getString(it) }
    } catch (e: Exception) {
        emptyMap()
    }

    fun noteApp(packageName: String, label: String) {
        val seen = seenApps()
        if (seen[packageName] == label || seen.size >= 200) return
        prefs.edit { putString("seen_apps", JSONObject(seen + (packageName to label)).toString()) }
    }

    // ---- things the phone looks up for the watch

    /** Off until the user turns it on: it sends the phone's rough position to a weather service. */
    var weatherOn: Boolean
        get() = prefs.getBoolean("weather", false)
        set(value) = prefs.edit { putBoolean("weather", value) }

    var calendarOn: Boolean
        get() = prefs.getBoolean("calendar", false)
        set(value) = prefs.edit { putBoolean("calendar", value) }

    /** The accent colour last chosen on the watch, so the app opens in it before the watch connects. */
    var accent: Int
        get() = prefs.getInt("accent", 0)
        set(value) = prefs.edit { putInt("accent", value) }

    /** Steps per day as the watch has reported them (see StepLog). */
    var stepLog: String?
        get() = prefs.getString("step_log", null)
        set(value) = prefs.edit { putString("step_log", value) }

    companion object {
        val DEFAULT_REPLIES = listOf("OK", "On my way", "Can't talk now", "Call you later", "Thanks")
    }
}

/** AES-GCM with a key held by the Android Keystore. */
private object Secrets {
    private const val ALIAS = "orbit.secrets"
    private const val TRANSFORM = "AES/GCM/NoPadding"

    private fun key(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val spec = KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build()
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply { init(spec) }.generateKey()
    }

    fun seal(plain: String): String {
        val cipher = Cipher.getInstance(TRANSFORM).apply { init(Cipher.ENCRYPT_MODE, key()) }
        val sealed = cipher.doFinal(plain.toByteArray(Charsets.UTF_8))
        return Base64.encodeToString(cipher.iv + sealed, Base64.NO_WRAP)
    }

    /** Null when the stored value cannot be read back (the phone's keystore was reset, say). */
    fun open(stored: String): String? = try {
        val bytes = Base64.decode(stored, Base64.NO_WRAP)
        val cipher = Cipher.getInstance(TRANSFORM).apply { init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, 12)) }
        String(cipher.doFinal(bytes, 12, bytes.size - 12), Charsets.UTF_8)
    } catch (e: Exception) {
        null
    }
}
