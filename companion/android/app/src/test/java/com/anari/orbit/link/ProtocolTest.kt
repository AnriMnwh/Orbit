package com.anari.orbit.link

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.sin

class ProtocolTest {

    /** Reads a line the way the watch's ble_gb.c does: skip control bytes, find GB( and the last ). */
    private fun asWatchReadsIt(line: ByteArray): JSONObject {
        val text = String(line.filter { it >= 0x20 || it < 0 }.toByteArray(), Charsets.UTF_8)
        val start = text.indexOf("GB(")
        return JSONObject(text.substring(start + 3, text.lastIndexOf(')')))
    }

    @Test
    fun `a line is wrapped like Gadgetbridge and survives the watch's parser`() {
        val body = "He said \"hi\" (twice)\nthen left \\ café ☕"
        val line = Link.line(JSONObject().put("t", "notify").put("id", 7).put("body", body))

        assertEquals(0x10, line.first().toInt())
        assertEquals('\n'.code, line.last().toInt())
        assertEquals(1, line.count { it == '\n'.code.toByte() })
        val read = asWatchReadsIt(line)
        assertEquals("notify", read.getString("t"))
        assertEquals(7, read.getInt("id"))
        assertEquals(body, read.getString("body"))
    }

    @Test
    fun `the clock line carries utc seconds and a fractional time zone`() {
        val line = String(Link.timeLine(1_700_000_000, 330), Charsets.UTF_8)
        assertTrue(line.contains("setTime(1700000000);"))
        assertTrue(line.contains("E.setTimeZone(5.5);"))
        assertTrue(line.endsWith("\n"))
    }

    @Test
    fun `hello names the version and what the app can do`() {
        val hello = Link.hello("0.1.0")
        assertEquals("orbit", hello.getString("t"))
        assertEquals(1, hello.getInt("v"))
        assertEquals("art", hello.getJSONArray("caps").getString(0))
    }

    @Test
    fun `crc matches the standard check value`() {
        assertEquals("cbf43926", Link.crc32Hex("123456789".toByteArray()))
    }

    @Test
    fun `lines split across packets are put back together`() {
        val lines = ArrayList<String>()
        val reader = LineReader { lines += it }
        reader.feed("{\"t\":\"sta".toByteArray())
        reader.feed("tus\",\"bat\":80}\n{\"t\":\"ver\"}\n{\"t\":".toByteArray())
        assertEquals(listOf("{\"t\":\"status\",\"bat\":80}", "{\"t\":\"ver\"}"), lines)
        reader.feed("\"x\"}\n".toByteArray())
        assertEquals("{\"t\":\"x\"}", lines.last())
    }

    @Test
    fun `an oversized line is dropped whole and the next one still arrives`() {
        val lines = ArrayList<String>()
        val reader = LineReader(limit = 8) { lines += it }
        reader.feed("0123456789abcdef\nok\n".toByteArray())
        assertEquals(listOf("ok"), lines)
    }

    @Test
    fun `a file cut into frames comes back whole`() {
        val file = ByteArray(1000) { (it * 7).toByte() }
        val frames = Link.frames(Link.KIND_ART, Link.split(file, Link.frameRoom(256)))

        assertEquals(4, frames.size)
        assertTrue(frames.all { it.size <= 256 - 3 })
        val reader = BulkReader().apply { open(Link.KIND_ART) }
        val back = frames.flatMap { reader.accept(it)!!.asIterable() }.toByteArray()
        assertArrayEquals(file, back)
        assertFalse(reader.broken)
    }

    @Test
    fun `the sequence number wraps after 255`() {
        val frames = Link.frames(Link.KIND_SPEECH, List(300) { ByteArray(1) })
        assertEquals(255, frames[255][1].toInt() and 0xFF)
        assertEquals(0, frames[256][1].toInt())
        val reader = BulkReader().apply { open(Link.KIND_SPEECH) }
        assertTrue(frames.all { reader.accept(it) != null })
    }

    @Test
    fun `a missing frame breaks the stream and other kinds are ignored`() {
        val frames = Link.frames(Link.KIND_MIC, List(3) { ByteArray(4) })
        val reader = BulkReader().apply { open(Link.KIND_MIC) }

        assertNull(reader.accept(Link.frames(Link.KIND_ART, listOf(ByteArray(4)))[0]))
        assertFalse(reader.broken)
        assertTrue(reader.accept(frames[0]) != null)
        assertNull(reader.accept(frames[2]))
        assertTrue(reader.broken)
    }

    @Test
    fun `text is clipped without splitting an emoji`() {
        assertEquals("abc", Link.clip("abc", 5))
        assertEquals("ab", Link.clip("ab😀cd", 3))
    }

    @Test
    fun `a long reply is broken at spaces into pieces that fit`() {
        val text = "The quick brown fox jumps over the lazy dog. ".repeat(12).trim()
        val parts = Link.chunkText(text, 80)
        assertTrue(parts.size > 1)
        assertTrue(parts.all { it.length <= 80 })
        assertEquals(text, parts.joinToString(""))
        assertEquals(listOf("short"), Link.chunkText("short", 80))
    }

    private fun tone(samples: Int, rate: Int, hz: Double): ShortArray =
        ShortArray(samples) { (sin(2 * PI * hz * it / rate) * 12000).toInt().toShort() }

    private fun snrDb(original: ShortArray, decoded: ShortArray): Double {
        var signal = 0.0
        var noise = 0.0
        for (i in original.indices) {
            signal += original[i].toDouble() * original[i]
            val e = (original[i] - decoded[i]).toDouble()
            noise += e * e
        }
        return 10 * log10(signal / noise)
    }

    @Test
    fun `speech survives the codec and each frame fits the packet`() {
        val pcm = tone(16000, 16000, 440.0)
        val room = Link.frameRoom(256)
        val frames = ImaAdpcm.encode(pcm, room)

        assertTrue(frames.all { it.size <= room })
        val decoded = frames.flatMap { ImaAdpcm.decode(it).asIterable() }.toShortArray()
        assertEquals(pcm.size, decoded.size)
        assertTrue("codec too lossy", snrDb(pcm, decoded) > 20)
        // A quarter of the size of raw 16-bit sound, plus small headers.
        assertTrue(frames.sumOf { it.size } < pcm.size * 2 / 4 + frames.size * ImaAdpcm.HEADER + 1)
    }

    @Test
    fun `any frame decodes on its own`() {
        val pcm = tone(4000, 16000, 300.0)
        val frames = ImaAdpcm.encode(pcm, 104)
        val perFrame = (104 - ImaAdpcm.HEADER) * 2

        val third = ImaAdpcm.decode(frames[2])
        val expected = pcm.copyOfRange(2 * perFrame, 3 * perFrame)
        assertTrue(snrDb(expected, third) > 20)
    }

    @Test
    fun `an odd number of samples is padded to an even frame`() {
        val frames = ImaAdpcm.encode(ShortArray(5) { 1000 }, 100)
        assertEquals(1, frames.size)
        assertEquals(ImaAdpcm.HEADER + 3, frames[0].size)
        assertEquals(6, ImaAdpcm.decode(frames[0]).size)
    }

    @Test
    fun `a wav file has the right header and length`() {
        val wav = Pcm.wav(ShortArray(100) { it.toShort() }, 16000)
        assertEquals(244, wav.size)
        assertEquals("RIFF", String(wav, 0, 4))
        assertEquals("WAVEfmt ", String(wav, 8, 8))
        assertEquals(16000, (wav[24].toInt() and 0xFF) or ((wav[25].toInt() and 0xFF) shl 8))
        assertEquals(99, wav[242].toInt())
    }

    @Test
    fun `resampling keeps the length in proportion and the tone intact`() {
        val in24 = tone(24000, 24000, 440.0)
        val out16 = Pcm.resample(in24, 24000, 16000)
        assertEquals(16000, out16.size)
        assertTrue(snrDb(tone(16000, 16000, 440.0), out16) > 20)
        assertArrayEquals(in24, Pcm.resample(in24, 24000, 24000))
    }

    @Test
    fun `the sample buffer grows and stops at its cap`() {
        val buffer = PcmBuffer(maxSamples = 40000)
        repeat(5) { buffer.add(ShortArray(10000) { 3 }) }
        assertEquals(40000, buffer.size)
        assertEquals(3, buffer.toArray().last().toInt())
    }

    @Test
    fun `little endian bytes become samples`() {
        assertArrayEquals(shortArrayOf(1, -2), Pcm.fromBytes(byteArrayOf(1, 0, 0xFE.toByte(), 0xFF.toByte())))
    }
}
