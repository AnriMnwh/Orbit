package com.anari.orbit

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL

object OtaUpdater {
    private const val MIN_BYTES = 100_000L
    private const val MAX_BYTES = 4_194_304L

    private val hostPort = Regex("^([a-z0-9.]+)(?::(\\d{1,5}))?$")

    /**
     * True only for `orbit.local` or a literal private IPv4 address, with an optional port. The
     * whole string is matched, so userinfo ("10.0.0.1@evil.com"), paths and look-alike names
     * ("192.168.evil.com") never get through to the URL.
     */
    fun isLocalHost(host: String): Boolean {
        val match = hostPort.matchEntire(host.trim().lowercase()) ?: return false
        val port = match.groupValues[2]
        if (port.isNotEmpty() && port.toInt() !in 1..65535) return false
        val name = match.groupValues[1]
        if (name == "orbit.local") return true
        val parts = name.split('.')
        if (parts.size != 4) return false
        val octets = parts.map { p -> if (p.isEmpty() || p.length > 3) return false else p.toIntOrNull() ?: return false }
        if (octets.any { it !in 0..255 }) return false
        return octets[0] == 10 ||
            (octets[0] == 192 && octets[1] == 168) ||
            (octets[0] == 172 && octets[1] in 16..31)
    }

    fun fileName(context: Context, uri: Uri): String {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) return c.getString(0) ?: "orbit.bin"
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "orbit.bin"
    }

    suspend fun upload(
        context: Context,
        uri: Uri,
        host: String,
        pin: String,
        onProgress: (Float) -> Unit,
    ): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val cleanHost = host.trim()
            val cleanPin = pin.trim()
            require(isLocalHost(cleanHost)) { "Host must be a local address (e.g. orbit.local or 192.168.x.x)." }
            require(cleanPin.length == 6 && cleanPin.all(Char::isDigit)) { "Enter the 6-digit PIN shown on the watch." }

            val size = context.contentResolver.openAssetFileDescriptor(uri, "r")?.use { it.length } ?: -1L
            require(size in MIN_BYTES..MAX_BYTES) { "File size does not look like an Orbit firmware image." }

            context.contentResolver.openInputStream(uri)?.use { input ->
                val buf = ByteArray(8192)
                val first = input.read(buf)
                require(first > 0 && (buf[0].toInt() and 0xFF) == 0xE9) {
                    "Not a valid ESP32 firmware .bin file."
                }

                val conn = (URL("http://$cleanHost/api/update").openConnection() as HttpURLConnection).apply {
                    requestMethod = "POST"
                    doOutput = true
                    connectTimeout = 8_000
                    readTimeout = 30_000
                    setFixedLengthStreamingMode(size)
                    setRequestProperty("Content-Type", "application/octet-stream")
                    setRequestProperty("X-Orbit-Pin", cleanPin)
                }
                try {
                    var sent = 0L
                    var lastPercent = 0
                    conn.outputStream.use { out ->
                        out.write(buf, 0, first)
                        sent += first
                        withContext(Dispatchers.Main) { onProgress(sent.toFloat() / size) }
                        while (true) {
                            val n = input.read(buf)
                            if (n <= 0) break
                            out.write(buf, 0, n)
                            sent += n
                            // One hop to the main thread per percent, not per 8 KB chunk.
                            val percent = (sent * 100 / size).toInt().coerceAtMost(99)
                            if (percent != lastPercent) {
                                lastPercent = percent
                                withContext(Dispatchers.Main) { onProgress(percent / 100f) }
                            }
                        }
                        out.flush()
                    }
                    val code = conn.responseCode
                    if (code !in 200..299) {
                        val msg = conn.errorStream?.bufferedReader()?.readText()?.take(120)
                            ?: conn.responseMessage
                            ?: "HTTP $code"
                        error(msg)
                    }
                    withContext(Dispatchers.Main) { onProgress(1f) }
                } finally {
                    conn.disconnect()
                }
            } ?: error("Could not open the selected file.")
        }
    }
}
