package com.anari.orbit

import android.content.Context
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.Handler
import android.util.Log
import com.anari.orbit.link.Link
import com.anari.orbit.link.Messages
import com.anari.orbit.link.WatchLink
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * The photos chosen for the watch, already cut to its round 466 x 466 screen.
 * The phone keeps its own copy of each so the Photos tab can show them, and
 * remembers which have reached the watch.
 */
class PhotoStore private constructor(context: Context) {
    data class Item(
        /** The picture's checksum; also its file name on the phone. */
        val key: String,
        val file: File,
        /** What the watch calls it once it has it; empty until then. */
        val watchName: String,
        /** Why the watch will never take this picture, if it said so; empty otherwise. */
        val refused: String = "",
    ) {
        val sent get() = watchName.isNotEmpty()

        /** Still to be sent. */
        val waiting get() = !sent && refused.isEmpty()
    }

    private val folder = File(context.filesDir, "photos").apply { mkdirs() }
    private val index = File(folder, "index.json")
    private val leftOnWatch = File(folder, "left-on-watch.txt")
    val items = MutableStateFlow(load())

    private fun load(): List<Item> = try {
        val saved = JSONArray(index.readText())
        (0 until saved.length()).map { saved.getJSONObject(it) }
            .map { Item(it.getString("key"), File(folder, it.getString("key") + ".jpg"), it.optString("name"), it.optString("refused")) }
            .filter { it.file.exists() }
    } catch (e: Exception) {
        emptyList()
    }

    private fun save(list: List<Item>) {
        val saved = list.map { JSONObject().put("key", it.key).put("name", it.watchName).put("refused", it.refused) }
        index.writeText(JSONArray(saved).toString())
        items.value = list
    }

    /** Cuts the picture to the watch's shape, saves it and queues it. Slow: call off the main thread. */
    fun add(context: Context, picture: Uri): Boolean = try {
        val bitmap = ImageDecoder.decodeBitmap(ImageDecoder.createSource(context.contentResolver, picture)) { decoder, info, _ ->
            decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
            // Phone photos are far larger than the watch needs; shrink while decoding to save memory.
            val smaller = minOf(info.size.width, info.size.height)
            decoder.setTargetSampleSize(maxOf(1, smaller / (SIZE * 2)))
        }
        val jpeg = Art.jpeg(Art.fit(bitmap, SIZE, SIZE), MAX_BYTES)
        val key = Link.crc32Hex(jpeg)
        synchronized(this) {
            if (items.value.none { it.key == key }) {
                val file = File(folder, "$key.jpg").apply { writeBytes(jpeg) }
                save(items.value + Item(key, file, ""))
            }
        }
        true
    } catch (e: Exception) {
        Log.w("OrbitPhotos", "could not prepare $picture: $e")
        false
    }

    @Synchronized
    fun markSent(key: String, watchName: String) = save(items.value.map { if (it.key == key) it.copy(watchName = watchName) else it })

    /** The watch said this picture is one it cannot use, so it is not offered again. */
    @Synchronized
    fun markRefused(key: String, why: String) = save(items.value.map { if (it.key == key) it.copy(refused = why) else it })

    /** The watch was reset or replaced: everything needs sending again. */
    @Synchronized
    fun markAllUnsent() = save(items.value.map { it.copy(watchName = "", refused = "") })

    @Synchronized
    fun remove(key: String): Item? {
        val item = items.value.firstOrNull { it.key == key } ?: return null
        item.file.delete()
        save(items.value - item)
        // If the watch has it, it must be told; that may have to wait until it is next connected.
        if (item.sent) leftOnWatch.appendText(item.watchName + "\n")
        return item
    }

    /** Names of photos deleted on the phone that the watch has not yet been told to delete. */
    @Synchronized
    fun takeLeftOnWatch(): List<String> {
        if (!leftOnWatch.exists()) return emptyList()
        val names = leftOnWatch.readLines().filter { it.isNotBlank() }
        leftOnWatch.delete()
        return names
    }

    companion object {
        const val SIZE = 466
        const val MAX_BYTES = 100_000

        @Volatile private var instance: PhotoStore? = null

        fun get(context: Context): PhotoStore =
            instance ?: synchronized(this) { instance ?: PhotoStore(context.applicationContext).also { instance = it } }
    }
}

/** Sends queued photos to the watch one at a time over the bulk channel. Runs on the link thread. */
class PhotoRelay(private val store: PhotoStore, private val handler: Handler, private val link: WatchLink) {
    /** True once the watch has said it takes photos over Bluetooth. */
    var enabled = false

    private var sending: String? = null
    private val retried = HashSet<String>()

    /** The watch's reason for not taking more photos just now ("full", "space" and so on), or empty. */
    private var stalled = ""

    private val gaveUp = Runnable {
        sending = null // no answer; try again on the next connection rather than loop now
    }
    private val again = Runnable { pump() }

    /**
     * Catches the watch up: deletions first, then the next photo it does not
     * have, one at a time. [fresh] is for when the user has added or removed
     * something, which is worth another try even if the watch had said no.
     */
    fun pump(fresh: Boolean = false) {
        if (fresh) stall("")
        if (!enabled || !link.up || !link.bulkReady || sending != null || stalled.isNotEmpty()) return
        store.takeLeftOnWatch().forEach { link.send(Messages.photoDelete(it)) }
        val next = store.items.value.firstOrNull { it.waiting } ?: return
        val bytes = try {
            next.file.readBytes()
        } catch (e: Exception) {
            store.remove(next.key)
            return pump()
        }
        sending = next.key
        link.send(Messages.photo(next.key, bytes))
        link.sendBulk(Link.frames(Link.KIND_PHOTO, Link.split(bytes, Link.frameRoom(link.mtu))))
        handler.removeCallbacks(gaveUp)
        handler.postDelayed(gaveUp, 90_000)
    }

    /** {"t":"blob","kind":"photo","key":"..","ok":true,"name":"p0007.jpg"} */
    fun onBlobReply(json: JSONObject) {
        val key = json.optString("key")
        if (key != sending) return
        handler.removeCallbacks(gaveUp)
        sending = null
        if (json.optBoolean("ok")) {
            store.markSent(key, json.optString("name", key))
            return pump()
        }
        val why = json.optString("why")
        Log.w("OrbitPhotos", "the watch refused a photo: $why")
        when (why) {
            // Passing trouble: it arrived damaged, or the watch was still saving the one before. One more try.
            "lost", "busy" -> if (retried.add(key)) handler.postDelayed(again, if (why == "busy") 3000 else 200) else stall(why)
            // This picture will never do; move on to the next.
            "bad", "size" -> {
                store.markRefused(key, why)
                pump()
            }
            // Library full, no room, or the watch is in trouble: stop until something changes.
            else -> stall(why.ifEmpty { "storage" })
        }
    }

    private fun stall(why: String) {
        stalled = why
        Hub.state.update { it.copy(photoProblem = why) }
    }

    fun stop() {
        handler.removeCallbacks(gaveUp)
        handler.removeCallbacks(again)
        sending = null
        enabled = false
        retried.clear()
        stall("")
    }
}
