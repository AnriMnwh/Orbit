package com.anari.orbit.link

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.util.UUID
import java.util.zip.CRC32

/**
 * Orbit Link, version 1: the phone's half of companion/PROTOCOL.md.
 *
 * Nothing in this package touches Android, so it runs in plain JVM tests and
 * can move into a shared module when the iPhone app is written.
 */
object Link {
    const val VERSION = 1

    /** Control channel: the serial service Gadgetbridge also uses. */
    val NUS: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    val NUS_RX: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e") // phone writes
    val NUS_TX: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e") // watch notifies

    /** Bulk channel: binary frames. */
    val BULK: UUID = UUID.fromString("4f524249-5400-4c4e-8b00-000000000001")
    val BULK_IN: UUID = UUID.fromString("4f524249-5400-4c4e-8b00-000000000002") // phone writes
    val BULK_OUT: UUID = UUID.fromString("4f524249-5400-4c4e-8b00-000000000003") // watch notifies

    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val KIND_ART = 1
    const val KIND_MIC = 2
    const val KIND_SPEECH = 3
    const val KIND_PHOTO = 16

    /** Longest line the phone may send; the watch drops anything over 1500 bytes whole. */
    const val MAX_LINE = 1000

    val CAPS = listOf(
        "art", "music2", "agent", "photos", "agenda", "place", "cfg", "act", "replies", "phone",
        "busy", "day",
    )

    /** One message to the watch, wrapped the way Gadgetbridge wraps it. */
    fun line(json: JSONObject): ByteArray = ("\u0010GB($json)\n").toByteArray(Charsets.UTF_8)

    /** Sets the watch's clock. The time zone is whole and fractional hours east of UTC. */
    fun timeLine(utcSeconds: Long, offsetMinutes: Int): ByteArray {
        val hours = offsetMinutes / 60.0
        return "\u0010setTime($utcSeconds);E.setTimeZone($hours);\n".toByteArray(Charsets.UTF_8)
    }

    fun hello(appVersion: String): JSONObject = JSONObject()
        .put("t", "orbit")
        .put("v", VERSION)
        .put("app", "android $appVersion")
        .put("caps", JSONArray(CAPS))

    /** CRC-32 as zlib computes it, in eight lower-case hex digits. */
    fun crc32Hex(data: ByteArray): String {
        val crc = CRC32()
        crc.update(data)
        return "%08x".format(crc.value)
    }

    /** Bytes of payload one bulk frame can carry at this packet size. */
    fun frameRoom(mtu: Int): Int = (mtu - 5).coerceAtLeast(1)

    /** Cuts a file into pieces that each fit one bulk frame. */
    fun split(data: ByteArray, room: Int): List<ByteArray> =
        (data.indices step room).map { data.copyOfRange(it, minOf(it + room, data.size)) }

    /** Puts the kind and a running sequence number in front of each payload. */
    fun frames(kind: Int, payloads: List<ByteArray>): List<ByteArray> = payloads.mapIndexed { i, payload ->
        ByteArray(payload.size + 2).also {
            it[0] = kind.toByte()
            it[1] = (i and 0xFF).toByte()
            payload.copyInto(it, 2)
        }
    }

    /** Cuts [data] into frames in one pass: the kind and sequence number, then up to [room] payload bytes. */
    fun frames(kind: Int, data: ByteArray, room: Int): List<ByteArray> =
        (data.indices step room).mapIndexed { i, from ->
            val to = minOf(from + room, data.size)
            ByteArray(to - from + 2).also {
                it[0] = kind.toByte()
                it[1] = (i and 0xFF).toByte()
                data.copyInto(it, 2, from, to)
            }
        }

    /** Shortens text to what a watch field holds, without cutting a character pair in half. */
    fun clip(text: String, maxChars: Int): String {
        if (text.length <= maxChars) return text
        var end = maxChars
        if (Character.isHighSurrogate(text[end - 1])) end--
        return text.substring(0, end)
    }

    /** Breaks a long reply at spaces so each piece fits comfortably in one line. */
    fun chunkText(text: String, maxChars: Int): List<String> {
        val out = ArrayList<String>()
        var rest = text.trim()
        while (rest.length > maxChars) {
            var cut = rest.lastIndexOf(' ', maxChars)
            if (cut < maxChars / 2) cut = clip(rest, maxChars).length
            out += rest.substring(0, cut)
            rest = rest.substring(cut)
        }
        if (rest.isNotEmpty() || out.isEmpty()) out += rest
        return out
    }
}

/** Turns the bytes the watch sends into whole lines. */
class LineReader(private val limit: Int = 64 * 1024, private val onLine: (String) -> Unit) {
    private val buffer = ByteArrayOutputStream()
    private var overflowed = false

    fun feed(bytes: ByteArray) {
        for (b in bytes) {
            if (b == '\n'.code.toByte()) {
                if (!overflowed && buffer.size() > 0) onLine(String(buffer.toByteArray(), Charsets.UTF_8))
                reset()
            } else if (buffer.size() < limit) {
                buffer.write(b.toInt())
            } else {
                overflowed = true
            }
        }
    }

    fun reset() {
        buffer.reset()
        overflowed = false
    }
}

/** Checks the frames of one incoming bulk stream arrive complete and in order. */
class BulkReader {
    private var kind = -1
    private var next = 0

    /** True once a frame of the open stream went missing; the stream is then useless. */
    var broken = false
        private set

    fun open(kind: Int) {
        this.kind = kind
        next = 0
        broken = false
    }

    fun close() {
        kind = -1
    }

    /** The frame's payload, or null when it is not the next frame of the open stream. */
    fun accept(frame: ByteArray): ByteArray? {
        if (frame.size < 2 || (frame[0].toInt() and 0xFF) != kind || broken) return null
        if ((frame[1].toInt() and 0xFF) != next) {
            broken = true
            return null
        }
        next = (next + 1) and 0xFF
        return frame.copyOfRange(2, frame.size)
    }
}
