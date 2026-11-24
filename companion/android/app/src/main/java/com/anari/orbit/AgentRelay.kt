package com.anari.orbit

import android.os.Handler
import android.util.Log
import com.anari.orbit.link.BulkReader
import com.anari.orbit.link.ImaAdpcm
import com.anari.orbit.link.Link
import com.anari.orbit.link.Pcm
import com.anari.orbit.link.PcmBuffer
import com.anari.orbit.link.WatchLink
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.concurrent.ConcurrentHashMap
import kotlin.coroutines.coroutineContext

/**
 * The assistant's phone half. The watch records a question and sends it as
 * speech frames; this turns it into text, asks the model, lets the model use
 * the watch's own tools, and sends back the answer as text and speech.
 *
 * Messages arrive on the link thread; the internet work runs on [scope].
 */
class AgentRelay(
    private val settings: Settings,
    private val scope: CoroutineScope,
    private val handler: Handler,
    private val link: WatchLink,
) {
    /** True once the watch has said it can route the assistant through the phone. */
    var enabled = false

    private val incoming = BulkReader()
    private var question = PcmBuffer(MAX_SECONDS * 16000)
    private var rate = 16000
    private var tools: JSONArray? = null
    private var turn: Job? = null
    private val history = ArrayList<JSONObject>() // alternating user and assistant messages
    private val waiting = ConcurrentHashMap<Int, CompletableDeferred<String>>()
    private var nextCall = 1

    /** A spoken reply was started and the link went away before the watch heard that it was over. */
    private var doneOwed = false

    fun reset() {
        turn?.cancel()
        turn = null
        incoming.close()
        waiting.clear()
        tools = null
        enabled = false
        synchronized(history) { history.clear() }
        question = PcmBuffer(MAX_SECONDS * rate)
    }

    /** The watch has said hello again: tell it about a reply that ended while the link was down. */
    fun linkUp() {
        if (!doneOwed) return
        doneOwed = false
        link.send(JSONObject().put("t", "agent").put("n", "done"))
    }

    // ---------------------------------------------------------- from the watch

    fun onLine(json: JSONObject) {
        when (json.optString("n")) {
            "tools" -> tools = json.optJSONArray("tools")
            "start" -> {
                cancel()
                rate = json.optInt("rate", 16000)
                question = PcmBuffer(MAX_SECONDS * rate)
                if (json.optString("codec", "ima") == "ima") incoming.open(Link.KIND_MIC) else say("error", "text", "This app is too old for the watch")
                status("Listening")
            }
            "stop" -> {
                val broken = incoming.broken
                incoming.close()
                val pcm = question.toArray()
                turn = scope.launch { answer(pcm, rate, broken) }
            }
            "cancel" -> {
                cancel()
                status("")
            }
            "result" -> waiting.remove(json.optInt("id"))?.complete(json.optString("out"))
        }
    }

    fun onBulk(frame: ByteArray) {
        incoming.accept(frame)?.let { question.add(ImaAdpcm.decode(it)) }
    }

    private fun cancel() {
        doneOwed = false // the watch has given up on that reply itself
        turn?.cancel()
        turn = null
        incoming.close()
        link.clearBulk()
    }

    // ------------------------------------------------------------ one question

    private suspend fun answer(pcm: ShortArray, rate: Int, broken: Boolean) {
        try {
            if (broken) throw OpenAi.Failure("The recording did not arrive whole")
            if (pcm.size < rate / 3) throw OpenAi.Failure("I didn't hear anything")
            val key = settings.openAiKey ?: throw OpenAi.Failure("Add your OpenAI key in the Orbit app")
            val api = OpenAi(key)

            status("Working out what you said")
            val heard = api.transcribe(settings.sttModel, Pcm.wav(pcm, rate))
            coroutineContext.ensureActive()
            say("heard", "text", Link.clip(heard, 159))

            status("Thinking")
            val reply = chat(api, heard)
            coroutineContext.ensureActive()
            val parts = Link.chunkText(Link.clip(reply, 470), 200)
            parts.forEachIndexed { i, part ->
                post(JSONObject().put("t", "agent").put("n", "reply").put("text", part).put("more", i < parts.lastIndex))
            }

            status("Speaking")
            speak(api, reply)
            status("")
        } catch (e: CancellationException) {
            throw e
        } catch (e: OpenAi.Failure) {
            say("error", "text", e.message ?: "Something went wrong")
            status(e.message ?: "")
        } catch (e: Exception) {
            Log.w(TAG, "assistant failed", e)
            say("error", "text", "Something went wrong")
            status("Something went wrong")
        }
    }

    /** Chat with tool use, as the watch does it: up to four rounds, then give up. */
    private suspend fun chat(api: OpenAi, question: String): String {
        val now = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm"))
        val messages = JSONArray()
        messages.put(message("system", "$PERSONA It is $now local time."))
        synchronized(history) { history.forEach { messages.put(it) } }
        messages.put(message("user", question))

        repeat(4) {
            val reply = api.chat(settings.chatModel, messages, tools)
            coroutineContext.ensureActive()
            val calls = reply.optJSONArray("tool_calls")
            if (calls == null || calls.length() == 0) {
                val text = reply.optString("content").trim().ifEmpty { throw OpenAi.Failure("The assistant had no answer") }
                synchronized(history) {
                    history += message("user", question)
                    history += message("assistant", text)
                    while (history.size > HISTORY_TURNS * 2) history.removeAt(0)
                }
                return text
            }
            // Echo the model's request back, then answer each call with what the watch did.
            messages.put(reply)
            for (i in 0 until calls.length()) {
                val call = calls.getJSONObject(i)
                val function = call.optJSONObject("function") ?: JSONObject()
                val args = try {
                    JSONObject(function.optString("arguments", "{}"))
                } catch (e: Exception) {
                    JSONObject()
                }
                val out = runOnWatch(function.optString("name"), args)
                messages.put(JSONObject().put("role", "tool").put("tool_call_id", call.optString("id")).put("content", out))
            }
        }
        throw OpenAi.Failure("The assistant got stuck")
    }

    /** Tools live on the watch; ask it to run one and wait for what it says. */
    private suspend fun runOnWatch(name: String, args: JSONObject): String {
        val id = synchronized(this) { nextCall++ }
        val result = CompletableDeferred<String>()
        waiting[id] = result
        post(JSONObject().put("t", "agent").put("n", "tool").put("id", id).put("name", name).put("args", args))
        return withTimeoutOrNull(5000) { result.await() } ?: "timed out".also { waiting.remove(id) }
    }

    /** A silent answer is fine: the words are already on the watch's screen. */
    private suspend fun speak(api: OpenAi, text: String) {
        val frames = try {
            val pcm = Pcm.resample(Pcm.fromBytes(api.speech(settings.ttsModel, settings.voice, text)), TTS_RATE, SPEECH_RATE)
            ImaAdpcm.encode(pcm, Link.frameRoom(link.mtu))
        } catch (e: OpenAi.Failure) {
            Log.w(TAG, "no speech: ${e.message}")
            emptyList()
        }
        coroutineContext.ensureActive()
        handler.post {
            // The watch plays a clip only if it fits its buffer, and only takes frames on an encrypted link.
            if (frames.isEmpty() || frames.sumOf { it.size } > MAX_SPEECH_BYTES || !link.bulkReady) {
                link.send(JSONObject().put("t", "agent").put("n", "done"))
                return@post
            }
            link.send(
                JSONObject().put("t", "agent").put("n", "speak").put("rate", SPEECH_RATE).put("codec", "ima")
                    .put("len", frames.sumOf { it.size }),
            )
            val queued = link.sendBulk(Link.frames(Link.KIND_SPEECH, frames)) {
                doneOwed = false
                link.send(JSONObject().put("t", "agent").put("n", "done"))
            }
            if (queued) doneOwed = true
            else link.send(JSONObject().put("t", "agent").put("n", "done"))
        }
    }

    // ---------------------------------------------------------------- helpers

    private fun message(role: String, content: String) = JSONObject().put("role", role).put("content", content)

    private fun say(what: String, field: String, value: String) =
        post(JSONObject().put("t", "agent").put("n", what).put(field, value))

    private fun post(json: JSONObject) {
        handler.post { link.send(json) }
    }

    private fun status(text: String) = Hub.state.update { it.copy(assistant = text) }

    private companion object {
        const val TAG = "OrbitAgent"
        const val MAX_SECONDS = 30
        const val HISTORY_TURNS = 6
        const val TTS_RATE = 24000
        const val SPEECH_RATE = 16000
        const val MAX_SPEECH_BYTES = 327_680 // the watch's limit; about 40 seconds

        // The same brief the watch gives the model when it asks over Wi-Fi.
        const val PERSONA = "You are Orbit, the voice assistant inside a small round smartwatch. Your answers are " +
            "spoken aloud and shown on a tiny screen: reply in one to three short sentences of plain text, with no " +
            "markdown, lists or emoji. Use the tools to check or control the watch when that helps."
    }
}
