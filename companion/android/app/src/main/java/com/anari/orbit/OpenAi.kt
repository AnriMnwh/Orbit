package com.anari.orbit

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * The three OpenAI calls behind the assistant, the same ones the watch makes
 * itself over Wi-Fi (main/platform/agent.c). All of them block; call from a
 * background thread.
 *
 * The watch gives up on a question 60 seconds after it last heard from the
 * phone, so no single call here may take longer than that: 10 seconds to
 * connect plus the reading time each call allows.
 */
class OpenAi(private val key: String) {
    /** Something went wrong, with a sentence short enough for the watch's screen. */
    class Failure(message: String) : Exception(message)

    /** Speech to text. [wav] is a complete WAV file. */
    fun transcribe(model: String, wav: ByteArray): String {
        val boundary = "orbitboundary7d1c"
        val body = ByteArrayOutputStream()
        body.write(
            ("--$boundary\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n$model\r\n" +
                "--$boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"q.wav\"\r\n" +
                "Content-Type: audio/wav\r\n\r\n").toByteArray(),
        )
        body.write(wav)
        body.write("\r\n--$boundary--\r\n".toByteArray())
        val reply = post("/audio/transcriptions", "multipart/form-data; boundary=$boundary", body.toByteArray(), 30_000)
        return JSONObject(String(reply, Charsets.UTF_8)).optString("text").trim()
            .ifEmpty { throw Failure("I didn't catch that") }
    }

    /** One round of chat. Returns the assistant's message, which may ask for tools. */
    fun chat(model: String, messages: JSONArray, tools: JSONArray?): JSONObject {
        val request = JSONObject().put("model", model).put("messages", messages).put("max_completion_tokens", 400)
        if (tools != null && tools.length() > 0) request.put("tools", tools)
        val reply = JSONObject(String(post("/chat/completions", "application/json", request.toString().toByteArray(), 40_000), Charsets.UTF_8))
        return reply.optJSONArray("choices")?.optJSONObject(0)?.optJSONObject("message")
            ?: throw Failure("The assistant had no answer")
    }

    /** Text to speech: raw 24 kHz mono 16-bit sound. */
    fun speech(model: String, voice: String, text: String): ByteArray {
        val request = JSONObject().put("model", model).put("voice", voice).put("input", text).put("response_format", "pcm")
        return post("/audio/speech", "application/json", request.toString().toByteArray(), 30_000)
    }

    private fun post(path: String, contentType: String, body: ByteArray, timeoutMs: Int): ByteArray {
        val connection = URL(API + path).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = "POST"
            connection.connectTimeout = 10_000
            connection.readTimeout = timeoutMs
            connection.doOutput = true
            connection.setRequestProperty("Authorization", "Bearer $key")
            connection.setRequestProperty("Content-Type", contentType)
            connection.setFixedLengthStreamingMode(body.size)
            connection.outputStream.use { it.write(body) }
            val status = connection.responseCode
            if (status != 200) throw Failure(explain(status))
            return connection.inputStream.use { it.readBytes() }
        } catch (e: IOException) {
            throw Failure("The phone could not reach OpenAI")
        } finally {
            connection.disconnect()
        }
    }

    private fun explain(status: Int) = when (status) {
        401 -> "OpenAI rejected the API key"
        429 -> "OpenAI says the account is out of quota"
        404 -> "That model name is not available"
        else -> "OpenAI error $status"
    }

    private companion object {
        const val API = "https://api.openai.com/v1"
    }
}
