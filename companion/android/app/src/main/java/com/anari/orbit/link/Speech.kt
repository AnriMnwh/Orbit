package com.anari.orbit.link

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The speech codec of Orbit Link: IMA ADPCM, four bits a sample.
 *
 * Every frame starts with the coder's state (predictor, step index), so a
 * frame can be decoded on its own and a lost one cannot spoil the next.
 */
object ImaAdpcm {
    const val HEADER = 4

    private val INDEX = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)
    private val STEP = intArrayOf(
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
    )

    /** How far the predictor moves for one four-bit code at this step size. */
    private fun delta(code: Int, step: Int): Int {
        var d = step shr 3
        if (code and 4 != 0) d += step
        if (code and 2 != 0) d += step shr 1
        if (code and 1 != 0) d += step shr 2
        return if (code and 8 != 0) -d else d
    }

    /**
     * Encodes speech into frames of at most [frameBytes] bytes each. A frame
     * holds an even number of samples; the last sample is repeated if needed.
     */
    fun encode(pcm: ShortArray, frameBytes: Int): List<ByteArray> {
        val perFrame = ((frameBytes - HEADER) * 2).coerceAtLeast(2)
        val frames = ArrayList<ByteArray>()
        var predictor = 0
        var index = 0
        var at = 0
        while (at < pcm.size) {
            val count = minOf(perFrame, pcm.size - at)
            val even = count + (count and 1)
            val frame = ByteArray(HEADER + even / 2)
            frame[0] = predictor.toByte()
            frame[1] = (predictor shr 8).toByte()
            frame[2] = index.toByte()
            for (i in 0 until even) {
                val sample = pcm[at + minOf(i, count - 1)].toInt()
                val step = STEP[index]
                var diff = sample - predictor
                var code = 0
                if (diff < 0) {
                    code = 8
                    diff = -diff
                }
                if (diff >= step) {
                    code = code or 4
                    diff -= step
                }
                if (diff >= step shr 1) {
                    code = code or 2
                    diff -= step shr 1
                }
                if (diff >= step shr 2) code = code or 1
                predictor = (predictor + delta(code, step)).coerceIn(-32768, 32767)
                index = (index + INDEX[code]).coerceIn(0, 88)
                val slot = HEADER + i / 2
                frame[slot] = (frame[slot].toInt() or (code shl (if (i and 1 == 0) 0 else 4))).toByte()
            }
            frames += frame
            at += count
        }
        return frames
    }

    /** Decodes one frame. Returns no samples for a frame too short to hold a header. */
    fun decode(frame: ByteArray): ShortArray {
        if (frame.size <= HEADER) return ShortArray(0)
        var predictor = (frame[0].toInt() and 0xFF) or (frame[1].toInt() shl 8)
        var index = (frame[2].toInt() and 0xFF).coerceIn(0, 88)
        val out = ShortArray((frame.size - HEADER) * 2)
        for (i in out.indices) {
            val byte = frame[HEADER + i / 2].toInt()
            val code = (if (i and 1 == 0) byte else byte shr 4) and 0x0F
            predictor = (predictor + delta(code, STEP[index])).coerceIn(-32768, 32767)
            index = (index + INDEX[code]).coerceIn(0, 88)
            out[i] = predictor.toShort()
        }
        return out
    }
}

/** A growing run of 16-bit mono samples. */
class PcmBuffer(private val maxSamples: Int) {
    private var data = ShortArray(16000)
    var size = 0
        private set

    /** Appends what fits; samples past the cap are dropped. */
    fun add(samples: ShortArray) {
        val take = minOf(samples.size, maxSamples - size)
        if (take <= 0) return
        if (size + take > data.size) data = data.copyOf(maxOf(data.size * 2, size + take))
        samples.copyInto(data, size, 0, take)
        size += take
    }

    fun toArray(): ShortArray = data.copyOf(size)
}

object Pcm {
    /** Little-endian 16-bit bytes, as OpenAI's "pcm" speech format delivers them. */
    fun fromBytes(bytes: ByteArray): ShortArray {
        val out = ShortArray(bytes.size / 2)
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer().get(out)
        return out
    }

    /** A complete WAV file for the samples. */
    fun wav(pcm: ShortArray, rate: Int): ByteArray {
        val dataBytes = pcm.size * 2
        val b = ByteBuffer.allocate(44 + dataBytes).order(ByteOrder.LITTLE_ENDIAN)
        b.put("RIFF".toByteArray()).putInt(36 + dataBytes).put("WAVEfmt ".toByteArray())
        b.putInt(16).putShort(1).putShort(1).putInt(rate).putInt(rate * 2).putShort(2).putShort(16)
        b.put("data".toByteArray()).putInt(dataBytes)
        b.asShortBuffer().put(pcm)
        return b.array()
    }

    /**
     * Changes the sample rate by straight-line interpolation. Going down, the
     * sound is smoothed first so high notes do not fold back as hiss.
     */
    fun resample(pcm: ShortArray, from: Int, to: Int): ShortArray {
        if (from == to || pcm.size < 2) return pcm
        val src = if (to < from) smooth(pcm) else pcm
        val out = ShortArray((pcm.size.toLong() * to / from).toInt())
        for (i in out.indices) {
            val pos = i.toDouble() * from / to
            val a = pos.toInt().coerceAtMost(src.size - 1)
            val b = (a + 1).coerceAtMost(src.size - 1)
            val t = pos - a
            out[i] = (src[a] + (src[b] - src[a]) * t).toInt().toShort()
        }
        return out
    }

    private fun smooth(pcm: ShortArray): ShortArray = ShortArray(pcm.size) { i ->
        val before = pcm[maxOf(i - 1, 0)]
        val after = pcm[minOf(i + 1, pcm.size - 1)]
        ((before + 2 * pcm[i] + after) / 4).toShort()
    }
}
