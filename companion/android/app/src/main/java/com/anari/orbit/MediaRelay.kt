package com.anari.orbit

import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.media.AudioManager
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.net.Uri
import android.os.Handler
import android.os.SystemClock
import android.support.v4.media.MediaMetadataCompat
import android.support.v4.media.RatingCompat
import android.support.v4.media.session.MediaControllerCompat
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat
import android.util.Log
import android.view.KeyEvent
import com.anari.orbit.link.Link
import com.anari.orbit.link.WatchLink
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Tells the watch what is playing on the phone, in any player, and carries
 * the watch's controls back. Runs on the link thread.
 *
 * Android only lets an app with notification access see other apps' players,
 * so nothing here works until that is granted on the setup screen.
 */
class MediaRelay(
    private val context: Context,
    private val handler: Handler,
    private val link: WatchLink,
    private val scope: CoroutineScope,
) {
    /** The picture the watch asked for in its hello. */
    data class ArtSpec(val width: Int, val height: Int, val maxBytes: Int)

    /** Null until the watch says it shows album art. */
    var artSpec: ArtSpec? = null

    private val sessions = context.getSystemService(MediaSessionManager::class.java)
    private val audio = context.getSystemService(AudioManager::class.java)
    private val listener = ComponentName(context, NotificationRelay::class.java)
    private var watching = false
    private var controller: MediaControllerCompat? = null

    // The current track, as last reported by the player.
    private var info = JSONObject()
    private var artJpeg: ByteArray? = null
    private var artKey = ""

    // Preparing a picture takes a while, so it runs off the link thread; only the newest track's result is used.
    private var artJob: Job? = null
    private var artGeneration = 0

    // What the watch has been told on this connection.
    private var sentInfo = ""
    private var sentState = ""
    private var sentPosition = 0
    private var sentAt = 0L
    private var sentPlaying = false
    private var deliveredArt: String? = null
    private var sendingArt: String? = null
    private var artRetried = false
    private var triedArt = ""
    private var artTries = 0

    private val publish = Runnable { publishNow() }
    private val artGaveUp = Runnable {
        sendingArt = null
        sendArt()
    }

    private val sessionsChanged = MediaSessionManager.OnActiveSessionsChangedListener { pick(it.orEmpty()) }

    private val playerEvents = object : MediaControllerCompat.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadataCompat?) = take(metadata)
        override fun onPlaybackStateChanged(state: PlaybackStateCompat?) = schedule()
        override fun onShuffleModeChanged(shuffleMode: Int) = schedule()
        override fun onRepeatModeChanged(repeatMode: Int) = schedule()
        override fun onSessionDestroyed() = refresh()
    }

    private val volumeChanged = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) = schedule()
    }

    /** Begins watching the phone's players. Harmless to call again. */
    fun start() {
        if (watching) return refresh()
        val active = try {
            sessions.getActiveSessions(listener)
        } catch (e: SecurityException) {
            return // no notification access yet
        }
        sessions.addOnActiveSessionsChangedListener(sessionsChanged, listener, handler)
        context.registerReceiver(volumeChanged, IntentFilter(VOLUME_CHANGED), null, handler, Context.RECEIVER_NOT_EXPORTED)
        watching = true
        pick(active)
    }

    /** The watch went away: stop listening and forget what it had been told. */
    fun stop() {
        handler.removeCallbacks(publish)
        handler.removeCallbacks(artGaveUp)
        if (watching) {
            sessions.removeOnActiveSessionsChangedListener(sessionsChanged)
            context.unregisterReceiver(volumeChanged)
            watching = false
        }
        controller?.unregisterCallback(playerEvents)
        controller = null
        artJob?.cancel()
        artGeneration++
        sentInfo = ""
        sentState = ""
        deliveredArt = null
        sendingArt = null
        triedArt = ""
        artSpec = null
    }

    /** Looks at the players afresh, for when something about the link or the players changed. */
    fun refresh() {
        if (!watching) return
        try {
            pick(sessions.getActiveSessions(listener), force = true)
        } catch (e: SecurityException) {
            Log.w(TAG, "notification access was taken away")
        }
    }

    /** Follows the player that is playing, else the one Android ranks first. */
    private fun pick(active: List<MediaController>, force: Boolean = false) {
        val best = active.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING } ?: active.firstOrNull()
        val current = controller
        if (!force && best != null && current != null && current.sessionToken.token == best.sessionToken) return
        current?.unregisterCallback(playerEvents)
        controller = best?.let {
            MediaControllerCompat(context, MediaSessionCompat.Token.fromToken(it.sessionToken)).apply {
                registerCallback(playerEvents, handler)
            }
        }
        take(controller?.metadata)
    }

    // ------------------------------------------------------------ to the watch

    private fun take(metadata: MediaMetadataCompat?) {
        fun text(vararg keys: String) = keys.firstNotNullOfOrNull { metadata?.getString(it)?.takeIf(String::isNotBlank) } ?: ""

        val title = text(MediaMetadataCompat.METADATA_KEY_TITLE, MediaMetadataCompat.METADATA_KEY_DISPLAY_TITLE)
        val artist = text(
            MediaMetadataCompat.METADATA_KEY_ARTIST,
            MediaMetadataCompat.METADATA_KEY_ALBUM_ARTIST,
            MediaMetadataCompat.METADATA_KEY_DISPLAY_SUBTITLE,
        )
        val seconds = (metadata?.getLong(MediaMetadataCompat.METADATA_KEY_DURATION) ?: 0L).coerceAtLeast(0) / 1000

        // Field lengths match what the watch keeps (main/core/orbit.h).
        info = JSONObject().put("t", "musicinfo")
            .put("artist", Link.clip(artist, 47))
            .put("album", Link.clip(text(MediaMetadataCompat.METADATA_KEY_ALBUM), 47))
            .put("track", Link.clip(title, 63))
            .put("dur", seconds)
            .put("app", controller?.packageName?.let(::appName) ?: "")
            .put("art", "")
        artJpeg = null
        artKey = ""
        Hub.state.update { it.copy(track = title, artist = artist, art = null) }

        artJob?.cancel()
        val generation = ++artGeneration
        val spec = artSpec
        if (spec == null || metadata == null) return schedule()
        // Decoding and encoding the picture can take a while; keep the link thread free for frames meanwhile.
        artJob = scope.launch(Dispatchers.Default) {
            val picture = try {
                findPicture(metadata, maxOf(spec.width, spec.height))?.let { Art.fit(it, spec.width, spec.height) }
            } catch (e: Exception) {
                Log.w(TAG, "could not prepare the album art: $e")
                null
            }
            var jpeg = picture?.let { Art.jpeg(it, spec.maxBytes) }
            // The watch wants exactly its own picture size, so a file over its limit cannot be shrunk to fit.
            if (jpeg != null && jpeg.size > spec.maxBytes) {
                Log.w(TAG, "the album art is ${jpeg.size} bytes, over the watch's ${spec.maxBytes}; not sent")
                jpeg = null
            }
            val key = jpeg?.let(Link::crc32Hex) ?: ""
            handler.post {
                if (generation != artGeneration) return@post
                artJpeg = jpeg
                artKey = key
                info.put("art", key)
                Hub.state.update { it.copy(art = picture) }
                schedule()
            }
        }
    }

    /** The first picture the player offers, decoded no larger than the watch needs ([target] pixels). */
    private fun findPicture(metadata: MediaMetadataCompat, target: Int): Bitmap? {
        for (key in listOf(
            MediaMetadataCompat.METADATA_KEY_ALBUM_ART,
            MediaMetadataCompat.METADATA_KEY_ART,
            MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON,
        )) metadata.getBitmap(key)?.let { return it }
        // Some players only give an address for the picture. Read it if it is on this phone.
        for (key in listOf(
            MediaMetadataCompat.METADATA_KEY_ALBUM_ART_URI,
            MediaMetadataCompat.METADATA_KEY_ART_URI,
            MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON_URI,
        )) {
            val uri = metadata.getString(key)?.let(Uri::parse) ?: continue
            if (uri.scheme != "content" && uri.scheme != "file" && uri.scheme != "android.resource") continue
            try {
                decodeSmall(uri, target)?.let { return it }
            } catch (e: Exception) {
                Log.i(TAG, "could not read the picture at $uri")
            }
        }
        return null
    }

    /** Reads the size first, then decodes at the largest reduction that still leaves [target] pixels on the short side. */
    private fun decodeSmall(uri: Uri, target: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        val shortSide = minOf(bounds.outWidth, bounds.outHeight)
        if (shortSide <= 0) return null
        var sample = 1
        while (shortSide / (sample * 2) >= target) sample *= 2
        val options = BitmapFactory.Options().apply { inSampleSize = sample }
        return context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
    }

    /** Players report in bursts; wait for the burst to end before telling the watch. */
    private fun schedule() {
        handler.removeCallbacks(publish)
        handler.postDelayed(publish, 300)
    }

    private fun publishNow() {
        if (!link.up) return
        val player = controller ?: return

        val infoText = info.toString()
        if (infoText != sentInfo) {
            sentInfo = infoText
            link.send(info)
        }
        sendArt()

        val state = player.playbackState
        val playing = state?.state == PlaybackStateCompat.STATE_PLAYING
        val position = state?.let(::positionNow) ?: 0
        val actions = state?.actions ?: 0L
        val can = JSONArray()
        if (actions and PlaybackStateCompat.ACTION_SEEK_TO != 0L) can.put("seek")
        if (actions and PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE != 0L) can.put("shuffle")
        if (actions and PlaybackStateCompat.ACTION_SET_REPEAT_MODE != 0L) can.put("repeat")
        if (likeAction(state) != null || player.ratingType in LIKE_RATINGS) can.put("like")
        val shuffle = if (player.shuffleMode == PlaybackStateCompat.SHUFFLE_MODE_ALL || player.shuffleMode == PlaybackStateCompat.SHUFFLE_MODE_GROUP) 1 else 0
        val repeat = when (player.repeatMode) {
            PlaybackStateCompat.REPEAT_MODE_ONE -> 2
            PlaybackStateCompat.REPEAT_MODE_ALL, PlaybackStateCompat.REPEAT_MODE_GROUP -> 1
            else -> 0
        }
        val message = JSONObject().put("t", "musicstate")
            .put("state", if (playing) "play" else "pause")
            .put("shuffle", shuffle).put("repeat", repeat)
            .put("vol", volumePercent()).put("can", can)

        // The watch counts the seconds itself, so only speak up when something
        // other than the passing of time changed.
        val now = SystemClock.elapsedRealtime()
        val expected = sentPosition + if (sentPlaying) ((now - sentAt) / 1000).toInt() else 0
        val stateText = message.toString()
        if (stateText == sentState && abs(position - expected) <= 2) return
        sentState = stateText
        sentPosition = position
        sentAt = now
        sentPlaying = playing
        link.send(message.put("position", position))
    }

    private fun positionNow(state: PlaybackStateCompat): Int {
        var ms = state.position
        if (state.state == PlaybackStateCompat.STATE_PLAYING) {
            ms += ((SystemClock.elapsedRealtime() - state.lastPositionUpdateTime) * state.playbackSpeed).toLong()
        }
        return (ms / 1000).toInt().coerceAtLeast(0)
    }

    private fun volumePercent(): Int {
        val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        return if (max > 0) audio.getStreamVolume(AudioManager.STREAM_MUSIC) * 100 / max else -1
    }

    private fun appName(packageName: String): String = try {
        val packages = context.packageManager
        packages.getApplicationLabel(packages.getApplicationInfo(packageName, 0)).toString()
    } catch (e: Exception) {
        ""
    }

    // -------------------------------------------------------------- album art

    /**
     * Sends the current track's picture unless the watch already has it or one
     * is on its way. If the watch says nothing the frames never reached it, so
     * the picture is announced afresh, three times at most.
     */
    fun sendArt() {
        val jpeg = artJpeg ?: return
        if (artSpec == null || !link.canSendBulk || sendingArt != null || artKey == deliveredArt) return
        if (artKey != triedArt) {
            triedArt = artKey
            artTries = 0
        }
        if (artTries >= 3) return
        val blob = JSONObject().put("t", "blob").put("kind", "art").put("key", artKey)
            .put("len", jpeg.size).put("crc", Link.crc32Hex(jpeg))
        link.send(blob)
        // Only a picture that is really on its way counts as tried and in flight.
        if (!link.sendBulk(Link.frames(Link.KIND_ART, jpeg, Link.frameRoom(link.mtu)))) return
        artTries++
        sendingArt = artKey
        handler.removeCallbacks(artGaveUp)
        handler.postDelayed(artGaveUp, 20_000)
    }

    /** The watch says whether the picture arrived intact. One more try if not. */
    fun onBlobReply(json: JSONObject) {
        if (json.optString("kind") != "art" || json.optString("key") != sendingArt) return
        handler.removeCallbacks(artGaveUp)
        val key = sendingArt
        sendingArt = null
        if (json.optBoolean("ok")) {
            deliveredArt = key
            artRetried = false
        } else if (artRetried) {
            deliveredArt = key // give up on this picture rather than loop
            artRetried = false
        } else {
            artRetried = true
        }
        sendArt()
    }

    // ---------------------------------------------------------- from the watch

    fun command(json: JSONObject) {
        val name = json.optString("n")
        val player = controller
        when {
            name == "volumeup" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, 0)
            name == "volumedown" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, 0)
            name == "vol" -> {
                val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
                val level = (json.optInt("v", 50).coerceIn(0, 100) * max / 100.0).roundToInt()
                audio.setStreamVolume(AudioManager.STREAM_MUSIC, level, 0)
            }
            // With no player open, a media key wakes whichever one played last.
            player == null -> MEDIA_KEYS[name]?.let(::mediaKey)
            else -> control(player, name, json)
        }
        schedule()
    }

    private fun control(player: MediaControllerCompat, name: String, json: JSONObject) {
        val controls = player.transportControls
        when (name) {
            "play" -> controls.play()
            "pause" -> controls.pause()
            "next" -> controls.skipToNext()
            "previous" -> controls.skipToPrevious()
            "seek" -> controls.seekTo(json.optLong("pos") * 1000)
            "shuffle" -> controls.setShuffleMode(
                when (player.shuffleMode) {
                    PlaybackStateCompat.SHUFFLE_MODE_ALL, PlaybackStateCompat.SHUFFLE_MODE_GROUP -> PlaybackStateCompat.SHUFFLE_MODE_NONE
                    else -> PlaybackStateCompat.SHUFFLE_MODE_ALL
                },
            )
            // Off, then the whole list, then the one track, as players' own buttons cycle.
            "repeat" -> controls.setRepeatMode(
                when (player.repeatMode) {
                    PlaybackStateCompat.REPEAT_MODE_ALL, PlaybackStateCompat.REPEAT_MODE_GROUP -> PlaybackStateCompat.REPEAT_MODE_ONE
                    PlaybackStateCompat.REPEAT_MODE_ONE -> PlaybackStateCompat.REPEAT_MODE_NONE
                    else -> PlaybackStateCompat.REPEAT_MODE_ALL
                },
            )
            "like" -> {
                val custom = likeAction(player.playbackState)
                when {
                    custom != null -> controls.sendCustomAction(custom, null)
                    player.ratingType == RatingCompat.RATING_HEART -> controls.setRating(RatingCompat.newHeartRating(true))
                    player.ratingType == RatingCompat.RATING_THUMB_UP_DOWN -> controls.setRating(RatingCompat.newThumbRating(true))
                }
            }
        }
    }

    /** Players have no standard "like" button; most offer one as a named extra action. */
    private fun likeAction(state: PlaybackStateCompat?): PlaybackStateCompat.CustomAction? =
        state?.customActions?.firstOrNull { LIKE.containsMatchIn(it.action) || LIKE.containsMatchIn(it.name) }

    private fun mediaKey(code: Int) {
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, code))
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code))
    }

    private companion object {
        const val TAG = "OrbitMedia"
        const val VOLUME_CHANGED = "android.media.VOLUME_CHANGED_ACTION"
        val LIKE = Regex("(?<!dis)(?<!un)like|heart|favou?rite|thumbs?.?up", RegexOption.IGNORE_CASE)
        val LIKE_RATINGS = setOf(RatingCompat.RATING_HEART, RatingCompat.RATING_THUMB_UP_DOWN)
        val MEDIA_KEYS = mapOf(
            "play" to KeyEvent.KEYCODE_MEDIA_PLAY,
            "pause" to KeyEvent.KEYCODE_MEDIA_PAUSE,
            "next" to KeyEvent.KEYCODE_MEDIA_NEXT,
            "previous" to KeyEvent.KEYCODE_MEDIA_PREVIOUS,
        )
    }
}

/** Shrinks album art to what the watch asked for. */
object Art {
    /** Centre-crops to the target shape and scales, on a plain in-memory bitmap. */
    fun fit(source: Bitmap, width: Int, height: Int): Bitmap {
        // Pictures handed over by other apps may live on the graphics chip, where they cannot be drawn from.
        val readable = if (source.config == Bitmap.Config.HARDWARE) source.copy(Bitmap.Config.ARGB_8888, false) else source
        val scale = maxOf(width.toFloat() / readable.width, height.toFloat() / readable.height)
        val cropW = (width / scale).toInt().coerceIn(1, readable.width)
        val cropH = (height / scale).toInt().coerceIn(1, readable.height)
        val left = (readable.width - cropW) / 2
        val top = (readable.height - cropH) / 2
        val out = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        Canvas(out).drawBitmap(
            readable,
            Rect(left, top, left + cropW, top + cropH),
            Rect(0, 0, width, height),
            Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG),
        )
        return out
    }

    /** JPEG at the best quality that fits the byte budget. */
    fun jpeg(picture: Bitmap, maxBytes: Int): ByteArray {
        var bytes = ByteArray(0)
        for (quality in intArrayOf(88, 80, 70, 60, 50, 40, 30)) {
            bytes = ByteArrayOutputStream().also { picture.compress(Bitmap.CompressFormat.JPEG, quality, it) }.toByteArray()
            if (bytes.size <= maxBytes) break
        }
        return bytes
    }
}
