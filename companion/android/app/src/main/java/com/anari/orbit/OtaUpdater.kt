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

    fun isLocalHost(host: String): Boolean {
        val clean = host.trim().lowercase().substringBefore(':')
        if (clean == "orbit.local") return true
        if (clean.startsWith("192.168.") || clean.startsWith("10.")) return true
        if (clean.startsWith("172.")) {
            val second = clean.removePrefix("172.").substringBefore('.').toIntOrNull() ?: return false
            return second in 16..31
        }
        return false
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
                    conn.outputStream.use { out ->
                        out.write(buf, 0, first)
                        sent += first
                        withContext(Dispatchers.Main) { onProgress(sent.toFloat() / size) }
                        while (true) {
                            val n = input.read(buf)
                            if (n <= 0) break
                            out.write(buf, 0, n)
                            sent += n
                            withContext(Dispatchers.Main) { onProgress((sent.toFloat() / size).coerceIn(0f, 0.99f)) }
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
