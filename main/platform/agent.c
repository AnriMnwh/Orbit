/* The assistant: record -> transcribe -> chat (with watch tools) -> speak.
 *
 * Three plain HTTPS calls to OpenAI per question. Model names can be changed
 * on the setup page because OpenAI retires them regularly; the defaults were
 * current in November 2026.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "http.h"
#include "platform.h"

static const char *TAG = "agent";

#define API "https://api.openai.com/v1"
#define DEFAULT_STT "gpt-transcribe"
#define DEFAULT_CHAT "gpt-6-luna"
#define DEFAULT_TTS "gpt-4o-mini-tts-2025-12-15"
#define DEFAULT_VOICE "marin"
#define TTS_RATE 24000
#define HISTORY_TURNS 6
#define BOUNDARY "orbitboundary7d1c"

static const char TOOLS[] =
    "[{\"type\":\"function\",\"function\":{\"name\":\"watch_status\",\"description\":\"Battery, time, steps, phone "
    "link, what is playing, unread notifications and the pet's mood.\",\"parameters\":{\"type\":\"object\","
    "\"properties\":{}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"media_control\",\"description\":\"Control music on the paired "
    "phone or computer.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\","
    "\"enum\":[\"play_pause\",\"next\",\"previous\",\"volume_up\",\"volume_down\"]}},\"required\":[\"action\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"start_timer\",\"description\":\"Start a countdown timer.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"seconds\":{\"type\":\"integer\"}},\"required\":[\"seconds\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"open_app\",\"description\":\"Open an app on the watch.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"app\":{\"type\":\"string\",\"enum\":[\"music\",\"keys\","
    "\"phone\",\"maps\",\"camera\",\"photos\",\"pet\",\"timer\",\"settings\",\"torch\",\"alarm\",\"stopwatch\","
    "\"weather\",\"fitness\",\"calc\",\"find\",\"sun\",\"exposure\",\"workout\",\"presenter\",\"world\",\"calendar\",\"agenda\",\"moon\"]}},\"required\":[\"app\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"set_brightness\",\"description\":\"Screen brightness, 5 to 100.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\"}},\"required\":[\"percent\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"do_not_disturb\",\"description\":\"Silence notifications.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}},\"required\":[\"on\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"camera\",\"description\":\"Trigger the connected LUMIX camera. "
    "Only works while the Camera app is connected.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"action\":"
    "{\"type\":\"string\",\"enum\":[\"shutter\",\"record_toggle\"]}},\"required\":[\"action\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"pet\",\"description\":\"Look after the virtual pet.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"feed\",\"play\","
    "\"wash\",\"sleep\",\"status\"]}},\"required\":[\"action\"]}}}]";

/* Settings are read from flash on the LVGL thread when listening starts. The
 * worker's stack lives in external RAM, and such a task must not touch flash. */
static struct {
    char key[200], stt[48], chat[48], tts[48], voice[24];
} cfg;

static TaskHandle_t task;
static esp_timer_handle_t level_timer;
static volatile bool cancelled;
static cJSON *history; /* alternating user / assistant text messages */

/* ------------------------------------------------------------- UI updates */

typedef struct {
    agent_status_t status;
    bool set_heard, set_reply;
    char heard[160], reply[480], error[80];
} update_t;

static void update_apply(void *arg)
{
    update_t *u = arg;
    S.agent.status = u->status;
    if (u->set_heard) strlcpy(S.agent.heard, u->heard, sizeof(S.agent.heard));
    if (u->set_reply) strlcpy(S.agent.reply, u->reply, sizeof(S.agent.reply));
    strlcpy(S.agent.error, u->error, sizeof(S.agent.error));
    if (u->status != AGENT_LISTENING && u->status != AGENT_SPEAKING) S.agent.level = 0;
    free(u);
    bus_emit(EVT_AGENT, NULL);
}

static void publish(agent_status_t status, const char *heard, const char *reply, const char *error)
{
    update_t *u = calloc(1, sizeof(*u));
    if (!u) return;
    u->status = status;
    if (heard) u->set_heard = true, strlcpy(u->heard, heard, sizeof(u->heard));
    if (reply) u->set_reply = true, strlcpy(u->reply, reply, sizeof(u->reply));
    if (error) strlcpy(u->error, error, sizeof(u->error));
    ui_post(update_apply, u);
}

static void level_tick(void *arg)
{
    LV_UNUSED(arg);
    S.agent.level = audio_level(); /* a single float; the glow reads it each frame */
}

/* ------------------------------------------------------------------ tools */

typedef struct {
    const char *name;
    cJSON *args;
    char result[360];
    SemaphoreHandle_t done;
} tool_call_t;

static const char *arg_str(cJSON *args, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(args, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

/* Runs on the LVGL thread, where it is safe to touch the watch's state. */
static void tool_run(void *arg)
{
    tool_call_t *t = arg;
    const char *n = t->name;
    snprintf(t->result, sizeof(t->result), "ok");

    if (strcmp(n, "watch_status") == 0) {
        struct tm now;
        char clock[16];
        svc_localtime(&now);
        fmt_clock(clock, sizeof(clock), &now, false);
        snprintf(t->result, sizeof(t->result),
                 "{\"time\":\"%s\",\"battery_percent\":%d,\"charging\":%s,\"steps\":%u,\"phone_connected\":%s,"
                 "\"playing\":\"%.40s - %.30s\",\"notifications\":%d,\"pet\":\"%.60s\"}",
                 clock, S.batt.percent, S.batt.charging ? "true" : "false", (unsigned)S.steps,
                 S.link.status == LINK_CONNECTED ? "true" : "false", S.media.title, S.media.artist, S.notif_count,
                 pet_command("status"));
    } else if (strcmp(n, "media_control") == 0) {
        const char *a = arg_str(t->args, "action");
        if (strcmp(a, "next") == 0) svc_media(MEDIA_NEXT);
        else if (strcmp(a, "previous") == 0) svc_media(MEDIA_PREV);
        else if (strcmp(a, "volume_up") == 0) svc_media(MEDIA_VOL_UP);
        else if (strcmp(a, "volume_down") == 0) svc_media(MEDIA_VOL_DOWN);
        else svc_media(MEDIA_TOGGLE);
    } else if (strcmp(n, "start_timer") == 0) {
        cJSON *s = cJSON_GetObjectItem(t->args, "seconds");
        timer_start(cJSON_IsNumber(s) ? s->valueint : 60);
    } else if (strcmp(n, "open_app") == 0) {
        const app_t *app = app_find(arg_str(t->args, "app"));
        if (app) shell_open(app);
        else snprintf(t->result, sizeof(t->result), "no such app");
    } else if (strcmp(n, "set_brightness") == 0) {
        cJSON *p = cJSON_GetObjectItem(t->args, "percent");
        int v = cJSON_IsNumber(p) ? p->valueint : 70;
        CFG.brightness = (uint8_t)(v < 5 ? 5 : (v > 100 ? 100 : v));
        settings_changed();
    } else if (strcmp(n, "do_not_disturb") == 0) {
        CFG.dnd = cJSON_IsTrue(cJSON_GetObjectItem(t->args, "on"));
        settings_changed();
    } else if (strcmp(n, "camera") == 0) {
        if (S.cam.status != CAM_READY) snprintf(t->result, sizeof(t->result), "camera is not connected");
        else svc_camera(strcmp(arg_str(t->args, "action"), "shutter") == 0 ? CAMCMD_SHUTTER : CAMCMD_RECORD_TOGGLE);
    } else if (strcmp(n, "pet") == 0) {
        snprintf(t->result, sizeof(t->result), "%s", pet_command(arg_str(t->args, "action")));
    } else {
        snprintf(t->result, sizeof(t->result), "unknown tool");
    }
    xSemaphoreGive(t->done);
}

/* ---- shared with link.c, which runs the same assistant through the phone ---- */

const char *agent_tools_json(void)
{
    return TOOLS;
}

void agent_run_tool(const char *name, cJSON *args, char *out, size_t len)
{
    tool_call_t t = {.name = name, .args = args, .done = xSemaphoreCreateBinary()};
    ui_post(tool_run, &t);
    if (xSemaphoreTake(t.done, pdMS_TO_TICKS(4000)) != pdTRUE) snprintf(t.result, sizeof(t.result), "timed out");
    ESP_LOGI(TAG, "tool %s -> %s", name, t.result);
    strlcpy(out, t.result, len);
    vSemaphoreDelete(t.done);
}

void agent_publish(agent_status_t status, const char *heard, const char *reply, const char *error)
{
    publish(status, heard, reply, error);
}

void agent_play(const int16_t *pcm, size_t samples, int rate)
{
    esp_timer_start_periodic(level_timer, 60000);
    audio_play(pcm, samples, rate);
    esp_timer_stop(level_timer);
}

/* ------------------------------------------------------------- API calls */

static void secret_or(const char *key, const char *fallback, char *out, size_t len)
{
    if (!net_secret_get(key, out, len)) strlcpy(out, fallback, len);
}

static const char *explain(int status)
{
    static char buf[64];
    if (status == 401) return "OpenAI rejected the API key";
    if (status == 429) return "OpenAI says the account is out of quota";
    if (status == 404) return "That model name is not available";
    if (status < 0) return "Could not reach OpenAI";
    snprintf(buf, sizeof(buf), "OpenAI error %d", status);
    return buf;
}

/* Speech to text. Returns malloc'd text or NULL (and sets *err). */
static char *transcribe(const char *key, const int16_t *pcm, size_t samples, const char **err)
{
    const char *model = cfg.stt;
    char head[400];
    int head_len = snprintf(head, sizeof(head),
                            "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
                            "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"q.wav\"\r\n"
                            "Content-Type: audio/wav\r\n\r\n",
                            model);
    static const char tail[] = "\r\n--" BOUNDARY "--\r\n";
    uint32_t data_bytes = (uint32_t)(samples * 2);
    size_t total = (size_t)head_len + 44 + data_bytes + sizeof(tail) - 1;
    uint8_t *body = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!body) {
        *err = "Out of memory";
        return NULL;
    }
    uint8_t *p = body;
    memcpy(p, head, (size_t)head_len), p += head_len;

    /* 44-byte WAV header: 16 kHz, mono, 16-bit. */
    uint32_t riff = 36 + data_bytes, rate = 16000, byte_rate = 32000, fmt_len = 16;
    uint16_t pcm_fmt = 1, channels = 1, align = 2, bits = 16;
    memcpy(p, "RIFF", 4), memcpy(p + 4, &riff, 4), memcpy(p + 8, "WAVEfmt ", 8), memcpy(p + 16, &fmt_len, 4);
    memcpy(p + 20, &pcm_fmt, 2), memcpy(p + 22, &channels, 2), memcpy(p + 24, &rate, 4), memcpy(p + 28, &byte_rate, 4);
    memcpy(p + 32, &align, 2), memcpy(p + 34, &bits, 2), memcpy(p + 36, "data", 4), memcpy(p + 40, &data_bytes, 4);
    p += 44;
    memcpy(p, pcm, data_bytes), p += data_bytes;
    memcpy(p, tail, sizeof(tail) - 1);

    http_request_t q = {.url = API "/audio/transcriptions", .bearer = key, .body = body, .body_len = total,
                        .content_type = "multipart/form-data; boundary=" BOUNDARY, .timeout_ms = 30000};
    http_reply_t r = {.max = 16 * 1024};
    int status = http_request(&q, &r);
    free(body);

    char *text = NULL;
    if (status == 200) {
        cJSON *j = cJSON_Parse(r.data);
        cJSON *t = j ? cJSON_GetObjectItem(j, "text") : NULL;
        if (cJSON_IsString(t) && t->valuestring[0]) text = strdup(t->valuestring);
        cJSON_Delete(j);
        if (!text) *err = "I didn't catch that";
    } else {
        ESP_LOGW(TAG, "transcribe: %d %.200s", status, r.data ? r.data : "");
        *err = explain(status);
    }
    http_free(&r);
    return text;
}

static void add_message(cJSON *list, const char *role, const char *content)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(list, m);
}

/* Chat with tool use. Returns malloc'd reply text or NULL (and sets *err). */
static char *chat(const char *key, const char *question, const char **err)
{
    const char *model = cfg.chat;
    char system[400];
    struct tm now;
    svc_localtime(&now);
    snprintf(system, sizeof(system),
             "You are Orbit, the voice assistant inside a small round smartwatch. Your answers are spoken aloud "
             "and shown on a tiny screen: reply in one to three short sentences of plain text, with no markdown, "
             "lists or emoji. Use the tools to check or control the watch when that helps. It is %04d-%02d-%02d "
             "%02d:%02d local time.",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min);

    cJSON *messages = cJSON_CreateArray();
    add_message(messages, "system", system);
    if (history) {
        cJSON *m;
        cJSON_ArrayForEach(m, history) cJSON_AddItemToArray(messages, cJSON_Duplicate(m, true));
    }
    add_message(messages, "user", question);

    char *answer = NULL;
    for (int round = 0; round < 4 && !answer && !*err && !cancelled; round++) {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "model", model);
        cJSON_AddItemToObject(req, "messages", cJSON_Duplicate(messages, true));
        cJSON_AddItemToObject(req, "tools", cJSON_Parse(TOOLS));
        cJSON_AddNumberToObject(req, "max_completion_tokens", 400);
        char *body = cJSON_PrintUnformatted(req);
        cJSON_Delete(req);
        if (!body) {
            *err = "Out of memory";
            break;
        }
        http_request_t q = {.url = API "/chat/completions", .bearer = key, .body = body, .body_len = strlen(body),
                            .content_type = "application/json", .timeout_ms = 40000};
        http_reply_t r = {.max = 48 * 1024};
        int status = http_request(&q, &r);
        free(body);
        if (status != 200) {
            ESP_LOGW(TAG, "chat: %d %.200s", status, r.data ? r.data : "");
            *err = explain(status);
            http_free(&r);
            break;
        }
        cJSON *j = cJSON_Parse(r.data);
        http_free(&r);
        cJSON *msg = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "choices"), 0), "message");
        cJSON *calls = cJSON_GetObjectItem(msg, "tool_calls");
        if (cJSON_IsArray(calls) && cJSON_GetArraySize(calls) > 0) {
            /* Echo the assistant's request back, then answer each call. */
            cJSON_AddItemToArray(messages, cJSON_Duplicate(msg, true));
            cJSON *call;
            cJSON_ArrayForEach(call, calls) {
                cJSON *fn = cJSON_GetObjectItem(call, "function");
                cJSON *id = cJSON_GetObjectItem(call, "id");
                cJSON *name = cJSON_GetObjectItem(fn, "name");
                cJSON *args = cJSON_GetObjectItem(fn, "arguments");
                tool_call_t t = {.name = cJSON_IsString(name) ? name->valuestring : "",
                                 .args = cJSON_Parse(cJSON_IsString(args) ? args->valuestring : "{}"),
                                 .done = xSemaphoreCreateBinary()};
                ui_post(tool_run, &t);
                if (xSemaphoreTake(t.done, pdMS_TO_TICKS(4000)) != pdTRUE) snprintf(t.result, sizeof(t.result), "timed out");
                ESP_LOGI(TAG, "tool %s -> %s", t.name, t.result);
                cJSON *out = cJSON_CreateObject();
                cJSON_AddStringToObject(out, "role", "tool");
                cJSON_AddStringToObject(out, "tool_call_id", cJSON_IsString(id) ? id->valuestring : "");
                cJSON_AddStringToObject(out, "content", t.result);
                cJSON_AddItemToArray(messages, out);
                cJSON_Delete(t.args);
                vSemaphoreDelete(t.done);
            }
        } else {
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (cJSON_IsString(content) && content->valuestring[0]) answer = strdup(content->valuestring);
            else *err = "The assistant had no answer";
        }
        cJSON_Delete(j);
    }
    cJSON_Delete(messages);
    if (!answer && !*err && !cancelled) *err = "The assistant got stuck";

    if (answer) {
        if (!history) history = cJSON_CreateArray();
        add_message(history, "user", question);
        add_message(history, "assistant", answer);
        while (cJSON_GetArraySize(history) > HISTORY_TURNS * 2) cJSON_DeleteItemFromArray(history, 0);
    }
    return answer;
}

/* Text to speech: fetches raw 24 kHz mono PCM and plays it. */
static void speak(const char *key, const char *text)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", cfg.tts);
    cJSON_AddStringToObject(req, "voice", cfg.voice);
    cJSON_AddStringToObject(req, "input", text);
    cJSON_AddStringToObject(req, "response_format", "pcm");
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return;

    http_request_t q = {.url = API "/audio/speech", .bearer = key, .body = body, .body_len = strlen(body),
                        .content_type = "application/json", .timeout_ms = 40000};
    http_reply_t r = {.max = 2600 * 1024}; /* about 50 seconds of speech */
    int status = http_request(&q, &r);
    free(body);
    if (status == 200 && r.len > 2000 && !cancelled) {
        esp_timer_start_periodic(level_timer, 60000);
        audio_play((const int16_t *)r.data, r.len / 2, TTS_RATE);
        esp_timer_stop(level_timer);
    } else if (status != 200) {
        /* The written answer is already on screen, so a silent reply is not an error. */
        ESP_LOGW(TAG, "speech: %d %.200s", status, r.data && status != 200 ? r.data : "");
    }
    http_free(&r);
}

/* ------------------------------------------------------------------- task */

static void run(void)
{
    const char *key = cfg.key;
    size_t samples = 0;
    esp_timer_stop(level_timer);
    int16_t *pcm = audio_record_stop(&samples);
    const char *err = NULL;
    char *heard = NULL, *reply = NULL;

    if (!pcm || samples < 16000 / 3) err = "I didn't hear anything";
    /* With the Orbit phone app connected, the phone does the internet part
     * and the watch needs neither Wi-Fi nor its own key. */
    if (!err && !cancelled && link_can_agent() && link_agent_ask(pcm, samples, &cancelled)) {
        free(pcm);
        return;
    }
    if (!err && !cfg.key[0]) err = "Add your OpenAI key on the setup page";
    bool acquired_net = false;
    if (!err && !net_online() && svc_configured(HAS_WIFI)) {
        net_acquire(NET_NEED_AGENT);
        acquired_net = true;
        for (int i = 0; i < 60 && !net_online() && !cancelled; i++) vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!err && !net_online()) err = "Wi-Fi is not connected";

    if (!err && !cancelled) heard = transcribe(key, pcm, samples, &err);
    free(pcm);
    if (heard && !cancelled) {
        publish(AGENT_THINKING, heard, "", NULL);
        reply = chat(key, heard, &err);
    }
    if (cancelled) {
        publish(AGENT_IDLE, NULL, NULL, NULL);
    } else if (reply) {
        publish(AGENT_SPEAKING, NULL, reply, NULL);
        speak(key, reply);
        publish(AGENT_IDLE, NULL, NULL, NULL);
    } else {
        publish(AGENT_ERROR, NULL, NULL, err ? err : "Something went wrong");
        svc_sound(SND_ERROR);
    }
    if (acquired_net) net_release(NET_NEED_AGENT);
    free(heard);
    free(reply);
}

static void agent_task(void *arg)
{
    LV_UNUSED(arg);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run();
    }
}

static void fail_now(const char *why)
{
    S.agent.status = AGENT_ERROR;
    strlcpy(S.agent.error, why, sizeof(S.agent.error));
    bus_emit(EVT_AGENT, NULL);
}

void svc_agent_listen(bool on)
{
    if (!task) return;
    if (on) {
        if (S.agent.status != AGENT_IDLE && S.agent.status != AGENT_ERROR) return;
        bool phone = link_can_agent(); /* the Orbit app answers; no Wi-Fi or key needed here */
        if (!net_secret_get("ai_key", cfg.key, sizeof(cfg.key)) && !phone) {
            return fail_now("Add your OpenAI key on the setup page");
        }
        secret_or("ai_stt", DEFAULT_STT, cfg.stt, sizeof(cfg.stt));
        secret_or("ai_chat", DEFAULT_CHAT, cfg.chat, sizeof(cfg.chat));
        secret_or("ai_tts", DEFAULT_TTS, cfg.tts, sizeof(cfg.tts));
        secret_or("ai_voice", DEFAULT_VOICE, cfg.voice, sizeof(cfg.voice));
        if (!S.wifi.connected && !phone) return fail_now("Wi-Fi is not connected");
        if (!audio_record_start()) return fail_now("The microphone is not available");
        cancelled = false;
        S.agent.status = AGENT_LISTENING;
        S.agent.heard[0] = S.agent.reply[0] = S.agent.error[0] = 0;
        esp_timer_start_periodic(level_timer, 60000);
        bus_emit(EVT_AGENT, NULL);
    } else if (S.agent.status == AGENT_LISTENING) {
        S.agent.status = AGENT_THINKING;
        bus_emit(EVT_AGENT, NULL);
        xTaskNotifyGive(task);
    }
}

void svc_agent_cancel(void)
{
    cancelled = true;
    link_agent_cancel();
    audio_stop();
    if (S.agent.status == AGENT_LISTENING) {
        size_t n;
        esp_timer_stop(level_timer);
        free(audio_record_stop(&n));
    }
    S.agent.status = AGENT_IDLE;
    S.agent.level = 0;
    bus_emit(EVT_AGENT, NULL);
}

void agent_init(void)
{
    const esp_timer_create_args_t t = {.callback = level_tick, .name = "agent-level"};
    esp_timer_create(&t, &level_timer);
    xTaskCreateWithCaps(agent_task, "agent", 12288, NULL, 4, &task, MALLOC_CAP_SPIRAM);
}
