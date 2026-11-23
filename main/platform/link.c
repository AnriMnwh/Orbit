/* What the Orbit phone app adds on top of the Gadgetbridge link.
 *
 * Protocol: companion/PROTOCOL.md (owned by the phone app's side). The app
 * speaks the same text lines as Gadgetbridge, so ble_gb.c still does the
 * everyday work; this file handles the three extras:
 *   hello   tells the watch it is the Orbit app, and starts pairing; the
 *           watch answers once the link is encrypted
 *   art     the current track's picture, as a small JPEG over the bulk channel
 *   agent   the assistant with the phone doing the internet part
 *
 * Three threads meet here. Lines and bulk frames arrive on Bluetooth's own
 * task, which must never wait; pictures are decoded on a small worker; a
 * question is asked from the assistant's task. Each block below says whose
 * its variables are.
 *
 * Written against the protocol document; never run against the app.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "platform.h"

static const char *TAG = "link";

#define ART_SIDE 240
#define ART_MAX 20000
#define KEY_LEN 16
#define SPEECH_MAX (320 * 1024) /* about 40 s of reply speech */
#define ANSWER_TIMEOUT_MS 60000

#define PHOTO_BLOB_MAX 100000

enum { BULK_ART = 1, BULK_MIC = 2, BULK_SPEECH = 3, BULK_PHOTO = 16 };

static volatile bool app, can_agent;
static volatile uint8_t phone_caps; /* LINK_CAP_ bits from the app's hello */

/* ------------------------------------------------------------ IMA ADPCM */

static const int16_t STEP[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,   23,   25,   28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,   88,   97,   107,  118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,  371,  408,  449,  494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411, 1552, 1707, 1878, 2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894, 6484, 7132, 7845, 8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
    int predictor, index;
} adpcm_t;

static int16_t adpcm_decode(adpcm_t *s, uint8_t code)
{
    int step = STEP[s->index], diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    s->predictor += code & 8 ? -diff : diff;
    s->predictor = s->predictor > 32767 ? 32767 : (s->predictor < -32768 ? -32768 : s->predictor);
    s->index += INDEX[code];
    s->index = s->index < 0 ? 0 : (s->index > 88 ? 88 : s->index);
    return (int16_t)s->predictor;
}

static uint8_t adpcm_encode(adpcm_t *s, int16_t sample)
{
    int step = STEP[s->index], diff = sample - s->predictor;
    uint8_t code = 0;
    if (diff < 0) code = 8, diff = -diff;
    if (diff >= step) code |= 4, diff -= step;
    if (diff >= step >> 1) code |= 2, diff -= step >> 1;
    if (diff >= step >> 2) code |= 1;
    adpcm_decode(s, code); /* keeps the encoder's state identical to the decoder's */
    return code;
}

/* -------------------------------------------------------------- album art */

/* A key names a track's picture. Only letters and digits are kept, so it can
 * go back to the phone inside a line without any escaping. */
static void key_copy(char *out, const char *in)
{
    size_t n = 0;
    for (; *in && n < KEY_LEN - 1; in++) {
        char c = *in;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out[n++] = c;
    }
    out[n] = 0;
}

/* The same CRC-32 zlib and PNG use. */
uint32_t link_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ---- on the LVGL thread: the one picture kept, and the track playing ---- */

static uint16_t *art_px;
static lv_image_dsc_t art_dsc;
static char art_key[KEY_LEN], track_key[KEY_LEN];

typedef struct {
    uint16_t *px;
    char key[KEY_LEN];
} art_ready_t;

const lv_image_dsc_t *svc_album_art(void)
{
    return art_px ? &art_dsc : NULL;
}

/* The picture shows only while it belongs to the track that is playing. */
static void art_show(void)
{
    bool on = art_px && art_key[0] && strcmp(art_key, track_key) == 0;
    if (!on && !S.media.has_art) return;
    S.media.has_art = on;
    bus_emit(EVT_MEDIA, NULL);
}

static void art_ready(void *arg)
{
    art_ready_t *r = arg;
    free(art_px);
    art_px = r->px;
    strlcpy(art_key, r->key, sizeof(art_key));
    art_dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)art_px,
    };
    free(r);
    art_show();
}

static void art_track(void *arg)
{
    strlcpy(track_key, arg, sizeof(track_key));
    free(arg);
    art_show();
}

/* The phone app went away: drop its picture and the extras only it reports. */
static void art_drop(void *arg)
{
    LV_UNUSED(arg);
    free(art_px);
    art_px = NULL;
    art_key[0] = track_key[0] = 0;
    S.media.has_art = false;
    S.media.shuffle = S.media.repeat = false;
    S.media.can_seek = S.media.can_shuffle = S.media.can_repeat = S.media.can_like = false;
    bus_emit(EVT_MEDIA, NULL);
    more_gone();
}

/* ---- on the worker: checking, decoding and enlarging a received file ---- */

typedef struct {
    uint8_t *jpg; /* the worker frees it */
    size_t len;
    uint32_t crc;
    char key[KEY_LEN];
} art_job_t;

static QueueHandle_t art_jobs;

void link_blob_reply(const char *kind, const char *key, bool ok, const char *extra, bool wait)
{
    char line[160];
    snprintf(line, sizeof(line), "{\"t\":\"blob\",\"kind\":\"%s\",\"key\":\"%s\",\"ok\":%s%s}", kind, key, ok ? "true" : "false", extra);
    if (wait) gb_send_wait(line);
    else gb_send(line);
}

/* Stretches the small picture to the whole screen and darkens it, so it can
 * sit behind the song title the way album art does on a phone's lock screen. */
static uint16_t *art_fill_screen(const uint16_t *src)
{
    uint16_t *out = heap_caps_malloc(SCR * SCR * 2, MALLOC_CAP_SPIRAM);
    if (!out) return NULL;
    const int scale = (ART_SIDE - 1) * 256 / (SCR - 1);
    for (int y = 0; y < SCR; y++) {
        int fy = y * scale, y0 = fy >> 8, wy = fy & 255;
        const uint16_t *r0 = src + y0 * ART_SIDE, *r1 = src + (y0 + 1 < ART_SIDE ? y0 + 1 : y0) * ART_SIDE;
        for (int x = 0; x < SCR; x++) {
            int fx = x * scale, x0 = fx >> 8, wx = fx & 255, x1 = x0 + 1 < ART_SIDE ? x0 + 1 : x0;
            uint16_t p[4] = {r0[x0], r0[x1], r1[x0], r1[x1]};
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                int shift = c == 0 ? 11 : (c == 1 ? 5 : 0), mask = c == 1 ? 63 : 31;
                int a = (p[0] >> shift) & mask, b = (p[1] >> shift) & mask;
                int d = (p[2] >> shift) & mask, e = (p[3] >> shift) & mask;
                int top = a * (256 - wx) + b * wx, bottom = d * (256 - wx) + e * wx;
                /* blend, then keep 40% of the brightness */
                rgb[c] = ((top * (256 - wy) + bottom * wy) >> 16) * 102 >> 8;
            }
            out[y * SCR + x] = (uint16_t)((rgb[0] << 11) | (rgb[1] << 5) | rgb[2]);
        }
    }
    return out;
}

static void art_task(void *arg)
{
    LV_UNUSED(arg);
    art_job_t job;
    for (;;) {
        if (xQueueReceive(art_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        bool ok = link_crc32(job.jpg, job.len) == job.crc;
        uint16_t *small = ok ? jpeg_calloc_align(ART_SIDE * ART_SIDE * 2, 16) : NULL;
        ok = small && jpeg_to_rgb565(job.jpg, job.len, ART_SIDE, ART_SIDE, small);
        free(job.jpg);
        art_ready_t *r = ok ? calloc(1, sizeof(*r)) : NULL;
        if (r) r->px = art_fill_screen(small);
        if (small) jpeg_free_align(small);
        ok = r && r->px;
        if (ok) {
            strlcpy(r->key, job.key, sizeof(r->key));
            ui_post(art_ready, r); /* the screen's thread owns it from here */
        } else {
            free(r);
            ESP_LOGW(TAG, "album art %s rejected", job.key);
        }
        link_blob_reply("art", job.key, ok, "", true);
    }
}

/* ---- on Bluetooth's task: collecting a file as its frames arrive ---- */

/* One file on its way in. Album art and photos each have their own, since a
 * track can change in the middle of a photo. */
typedef struct {
    char key[KEY_LEN];
    uint8_t *buf; /* NULL when nothing is being received */
    size_t len, got;
    uint32_t crc;
    uint8_t seq;
} rx_t;

static rx_t art_rx, photo_rx;

static void rx_drop(rx_t *rx)
{
    free(rx->buf);
    rx->buf = NULL;
}

/* Starts a file from its "blob" line. False if its size is out of range or
 * there is no memory for it. */
static bool rx_begin(rx_t *rx, cJSON *msg, size_t min, size_t max)
{
    cJSON *key = cJSON_GetObjectItem(msg, "key"), *len = cJSON_GetObjectItem(msg, "len"), *crc = cJSON_GetObjectItem(msg, "crc");
    rx_drop(rx);
    key_copy(rx->key, cJSON_IsString(key) ? key->valuestring : "");
    rx->len = cJSON_IsNumber(len) && len->valuedouble > 0 ? (size_t)len->valuedouble : 0;
    rx->crc = cJSON_IsString(crc) ? (uint32_t)strtoul(crc->valuestring, NULL, 16) : 0;
    rx->got = 0, rx->seq = 0;
    if (rx->len >= min && rx->len <= max) rx->buf = heap_caps_malloc(rx->len, MALLOC_CAP_SPIRAM);
    return rx->buf != NULL;
}

/* Adds one frame. 0: more to come, 1: the file is whole, -1: a frame went
 * missing and the file has been dropped. */
static int rx_frame(rx_t *rx, uint8_t seq, const uint8_t *payload, size_t len)
{
    if (seq != rx->seq) {
        rx_drop(rx);
        return -1;
    }
    rx->seq++;
    size_t take = len < rx->len - rx->got ? len : rx->len - rx->got;
    memcpy(rx->buf + rx->got, payload, take);
    rx->got += take;
    return rx->got >= rx->len;
}

static void art_begin(cJSON *msg)
{
    if (!art_jobs || !rx_begin(&art_rx, msg, 100, ART_MAX)) link_blob_reply("art", art_rx.key, false, "", false);
}

static void art_frame(uint8_t seq, const uint8_t *payload, size_t len)
{
    int state = rx_frame(&art_rx, seq, payload, len);
    if (state == 0) return;
    if (state > 0) {
        art_job_t job = {.jpg = art_rx.buf, .len = art_rx.len, .crc = art_rx.crc};
        strlcpy(job.key, art_rx.key, sizeof(job.key));
        art_rx.buf = NULL; /* the worker's now */
        if (xQueueSend(art_jobs, &job, 0) == pdTRUE) return;
        free(job.jpg);
    }
    link_blob_reply("art", art_rx.key, false, "", false); /* the phone may send it once more */
}

static void photo_begin(cJSON *msg)
{
    const char *why = NULL;
    if (more_photo_busy()) why = "busy";
    else if (!rx_begin(&photo_rx, msg, 200, PHOTO_BLOB_MAX)) why = "size";
    if (!why) return;
    char extra[32];
    snprintf(extra, sizeof(extra), ",\"why\":\"%s\"", why);
    /* On "busy" the key in hand is the earlier photo's, so read this one's. */
    char key[KEY_LEN];
    cJSON *k = cJSON_GetObjectItem(msg, "key");
    key_copy(key, cJSON_IsString(k) ? k->valuestring : "");
    link_blob_reply("photo", key, false, extra, false);
}

static void photo_frame(uint8_t seq, const uint8_t *payload, size_t len)
{
    int state = rx_frame(&photo_rx, seq, payload, len);
    if (state < 0) {
        link_blob_reply("photo", photo_rx.key, false, ",\"why\":\"lost\"", false);
    } else if (state > 0) {
        uint8_t *jpg = photo_rx.buf;
        photo_rx.buf = NULL; /* handed over */
        more_photo(jpg, photo_rx.len, photo_rx.crc, photo_rx.key);
    }
}

void link_art_key(const char *key)
{
    char *k = malloc(KEY_LEN);
    if (!k) return;
    key_copy(k, key);
    ui_post(art_track, k);
}

/* -------------------------------------------------------------- assistant */

enum { EV_HEARD, EV_TOOL, EV_REPLY, EV_SPEECH, EV_DONE, EV_ERROR };

/* What the phone said, handed from Bluetooth's task to the assistant's. */
typedef struct {
    uint8_t kind;
    bool more;
    int id;
    char *text;   /* heard / reply / error text, or the tool's name */
    char *args;   /* tool arguments as JSON text */
    int16_t *pcm; /* reply speech, ready to play */
    size_t samples;
    int rate;
} event_t;

static QueueHandle_t events;
static volatile bool asking, agent_cancel;

/* On Bluetooth's task: reply speech is decoded as it arrives, then handed
 * over whole. */
static int16_t *speech;
static size_t speech_bytes, speech_got, speech_samples;
static int speech_rate;
static uint8_t speech_seq;

static void event_free(event_t *e)
{
    free(e->text), free(e->args), free(e->pcm);
}

static void event_send(event_t *e)
{
    if (!events || xQueueSend(events, e, 0) != pdTRUE) event_free(e);
}

static void event_push(uint8_t kind, int id, bool more, const char *text, const char *args)
{
    event_t e = {.kind = kind, .id = id, .more = more, .text = text ? strdup(text) : NULL, .args = args ? strdup(args) : NULL};
    event_send(&e);
}

static void events_flush(void)
{
    event_t e;
    while (events && xQueueReceive(events, &e, 0) == pdTRUE) event_free(&e);
}

static void speech_begin(cJSON *msg)
{
    cJSON *len = cJSON_GetObjectItem(msg, "len"), *rate = cJSON_GetObjectItem(msg, "rate");
    free(speech);
    speech_bytes = cJSON_IsNumber(len) && len->valuedouble > 0 ? (size_t)len->valuedouble : 0;
    speech_rate = cJSON_IsNumber(rate) && rate->valueint >= 8000 && rate->valueint <= 48000 ? rate->valueint : 16000;
    speech_got = 0, speech_samples = 0, speech_seq = 0;
    /* every byte holds at most two samples */
    speech = speech_bytes > 0 && speech_bytes <= SPEECH_MAX ? heap_caps_malloc(speech_bytes * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM) : NULL;
}

static void speech_frame(uint8_t seq, const uint8_t *payload, size_t len)
{
    if (seq != speech_seq) {
        /* A gap would turn into noise. The written answer is already on
         * screen, so stay silent rather than play a damaged one. */
        ESP_LOGW(TAG, "reply speech lost a frame, not playing it");
        free(speech);
        speech = NULL;
        return;
    }
    speech_seq++;
    if (len >= 4) {
        adpcm_t s = {.predictor = (int16_t)(payload[0] | (payload[1] << 8)), .index = payload[2] > 88 ? 88 : payload[2]};
        for (size_t i = 4; i < len && speech_samples + 2 <= speech_bytes * 2; i++) {
            speech[speech_samples++] = adpcm_decode(&s, payload[i] & 0x0F);
            speech[speech_samples++] = adpcm_decode(&s, payload[i] >> 4);
        }
    }
    speech_got += len;
    if (speech_got < speech_bytes) return;

    event_t e = {.kind = EV_SPEECH, .pcm = speech, .samples = speech_samples, .rate = speech_rate};
    speech = NULL;
    event_send(&e);
}

void link_bulk_rx(const uint8_t *frame, size_t len)
{
    if (len < 3) return;
    if (frame[0] == BULK_ART && art_rx.buf) art_frame(frame[1], frame + 2, len - 2);
    else if (frame[0] == BULK_PHOTO && photo_rx.buf) photo_frame(frame[1], frame + 2, len - 2);
    else if (frame[0] == BULK_SPEECH && speech) speech_frame(frame[1], frame + 2, len - 2);
}

static void send_hello(void)
{
    char line[448];
    snprintf(line, sizeof(line),
             "{\"t\":\"orbit\",\"v\":1,\"fw\":\"Orbit %s\",\"caps\":[\"art\",\"music2\",\"agent\","
             "\"place\",\"agenda\",\"photos\",\"cfg\",\"act\",\"replies\",\"phone\","
             "\"vision\",\"zzz\",\"busy\",\"day\"],"
             "\"art\":{\"w\":%d,\"h\":%d,\"max\":%d},\"agenda\":{\"max\":%d,\"days\":%d},\"mtu\":256}",
             ORBIT_VERSION, ART_SIDE, ART_SIDE, ART_MAX, AGENDA_MAX, AGENDA_DAYS);
    gb_send(line);

    /* The watch owns the assistant's tool list; the phone just passes it on. */
    const char *tools = agent_tools_json();
    size_t n = strlen(tools) + 48;
    char *big = malloc(n);
    if (!big) return;
    snprintf(big, n, "{\"t\":\"agent\",\"n\":\"tools\",\"tools\":%s}", tools);
    gb_send(big);
    free(big);
}

static void agent_message(cJSON *msg)
{
    cJSON *n = cJSON_GetObjectItem(msg, "n"), *text = cJSON_GetObjectItem(msg, "text");
    const char *what = cJSON_IsString(n) ? n->valuestring : "";
    const char *t = cJSON_IsString(text) ? text->valuestring : "";
    if (strcmp(what, "heard") == 0) {
        event_push(EV_HEARD, 0, false, t, NULL);
    } else if (strcmp(what, "reply") == 0) {
        event_push(EV_REPLY, 0, cJSON_IsTrue(cJSON_GetObjectItem(msg, "more")), t, NULL);
    } else if (strcmp(what, "error") == 0) {
        event_push(EV_ERROR, 0, false, t, NULL);
    } else if (strcmp(what, "done") == 0) {
        event_push(EV_DONE, 0, false, NULL, NULL);
    } else if (strcmp(what, "tool") == 0) {
        cJSON *name = cJSON_GetObjectItem(msg, "name"), *id = cJSON_GetObjectItem(msg, "id");
        char *args = cJSON_PrintUnformatted(cJSON_GetObjectItem(msg, "args"));
        event_push(EV_TOOL, cJSON_IsNumber(id) ? id->valueint : 0, false, cJSON_IsString(name) ? name->valuestring : "",
                   args ? args : "{}");
        free(args);
    } else if (strcmp(what, "speak") == 0) {
        speech_begin(msg);
    }
}

/* On Bluetooth's task: the app said hello and has not been answered yet. */
static bool hello_due;
static uint8_t hello_caps;

void link_encrypted(void)
{
    if (!hello_due) return;
    hello_due = false;
    phone_caps = hello_caps;
    can_agent = (hello_caps & LINK_CAP_AGENT) && events;
    app = true;
    ble_set_app(true);
    send_hello();
    more_hello();
}

bool link_has(int cap)
{
    return app && (phone_caps & cap);
}

bool link_handle(const char *type, cJSON *msg)
{
    if (strcmp(type, "orbit") == 0) {
        if (!events) events = xQueueCreate(12, sizeof(event_t));
        if (!art_jobs) {
            /* Decoding a picture is too much work for Bluetooth's own task. */
            art_jobs = xQueueCreate(2, sizeof(art_job_t));
            if (art_jobs) xTaskCreateWithCaps(art_task, "art", 8192, NULL, 2, NULL, MALLOC_CAP_SPIRAM);
        }
        cJSON *v;
        hello_caps = 0;
        cJSON_ArrayForEach(v, cJSON_GetObjectItem(msg, "caps")) {
            const char *cap = cJSON_IsString(v) ? v->valuestring : "";
            if (strcmp(cap, "agent") == 0) hello_caps |= LINK_CAP_AGENT;
            else if (strcmp(cap, "cfg") == 0) hello_caps |= LINK_CAP_CFG;
            else if (strcmp(cap, "act") == 0) hello_caps |= LINK_CAP_ACT;
            else if (strcmp(cap, "vision") == 0) hello_caps |= LINK_CAP_VISION;
            else if (strcmp(cap, "zzz") == 0) hello_caps |= LINK_CAP_ZZZ;
            else if (strcmp(cap, "busy") == 0) hello_caps |= LINK_CAP_BUSY;
            else if (strcmp(cap, "day") == 0) hello_caps |= LINK_CAP_DAY;
        }
        /* Unlike Gadgetbridge, this link is encrypted. The answer waits for
         * that, so to the app "hello answered" also means pictures and speech
         * are safe to send. If pairing is declined there is no answer, and
         * both sides carry on as they would with Gadgetbridge. */
        hello_due = true;
        ble_request_pairing();
        if (ble_encrypted()) link_encrypted();
        return true;
    }
    if (strcmp(type, "blob") == 0) {
        cJSON *kind = cJSON_GetObjectItem(msg, "kind");
        const char *what = cJSON_IsString(kind) ? kind->valuestring : "";
        if (strcmp(what, "art") == 0) art_begin(msg);
        else if (strcmp(what, "photo") == 0) photo_begin(msg);
        return true;
    }
    if (strcmp(type, "agent") == 0) {
        if (asking) agent_message(msg); /* an answer nobody is waiting for is dropped */
        return true;
    }
    return more_handle(type, msg);
}

/* ---- on the assistant's task ---- */

static void send_question(const int16_t *pcm, size_t samples)
{
    static uint8_t frame[260];
    gb_send_wait("{\"t\":\"agent\",\"n\":\"start\",\"rate\":16000,\"codec\":\"ima\"}");
    /* Each frame: kind, sequence, the coder's state, then two samples a byte. */
    size_t room = (size_t)ble_payload();
    if (room > sizeof(frame)) room = sizeof(frame);
    size_t per_frame = (room - 6) * 2;
    adpcm_t s = {0, 0};
    uint8_t seq = 0;
    for (size_t at = 0; at < samples && !agent_cancel && app; at += per_frame) {
        size_t n = samples - at < per_frame ? samples - at : per_frame;
        n &= ~(size_t)1; /* whole bytes only */
        if (!n) break;
        frame[0] = BULK_MIC, frame[1] = seq++;
        frame[2] = (uint8_t)(s.predictor & 0xFF), frame[3] = (uint8_t)((s.predictor >> 8) & 0xFF);
        frame[4] = (uint8_t)s.index, frame[5] = 0;
        for (size_t i = 0; i < n; i += 2) {
            uint8_t low = adpcm_encode(&s, pcm[at + i]);
            frame[6 + i / 2] = (uint8_t)(low | (adpcm_encode(&s, pcm[at + i + 1]) << 4));
        }
        ble_tx_bulk(frame, 6 + n / 2, true);
    }
    gb_send_wait(agent_cancel ? "{\"t\":\"agent\",\"n\":\"cancel\"}" : "{\"t\":\"agent\",\"n\":\"stop\"}");
}

static void answer_tool(const event_t *e)
{
    char out[360];
    cJSON *args = cJSON_Parse(e->args ? e->args : "{}");
    agent_run_tool(e->text ? e->text : "", args, out, sizeof(out));
    cJSON_Delete(args);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "t", "agent");
    cJSON_AddStringToObject(r, "n", "result");
    cJSON_AddNumberToObject(r, "id", e->id);
    cJSON_AddStringToObject(r, "out", out);
    char *line = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (line) gb_send_wait(line);
    free(line);
}

/* Asks the phone. Returns false only if the phone cannot take the question,
 * in which case the caller falls back to the watch's own Wi-Fi. */
bool link_agent_ask(const int16_t *pcm, size_t samples, volatile bool *cancelled)
{
    if (!can_agent || !app || !events) return false;
    events_flush(); /* nothing stale */
    agent_cancel = false;
    asking = true;
    send_question(pcm, samples);

    static char reply[480];
    reply[0] = 0;
    bool finished = false;
    uint32_t waited = 0; /* since the phone last said anything */
    while (!finished) {
        event_t e;
        if (!app) {
            agent_publish(AGENT_ERROR, NULL, NULL, "The phone disconnected");
            finished = true;
        } else if (*cancelled || agent_cancel) {
            gb_send_wait("{\"t\":\"agent\",\"n\":\"cancel\"}");
            agent_publish(AGENT_IDLE, NULL, NULL, NULL);
            finished = true;
        } else if (xQueueReceive(events, &e, pdMS_TO_TICKS(250)) != pdTRUE) {
            waited += 250;
            if (waited >= ANSWER_TIMEOUT_MS) {
                agent_publish(AGENT_ERROR, NULL, NULL, "The phone did not answer");
                finished = true;
            }
        } else {
            waited = 0;
            switch (e.kind) {
            case EV_HEARD: agent_publish(AGENT_THINKING, e.text, "", NULL); break;
            case EV_TOOL: answer_tool(&e); break;
            case EV_REPLY:
                strlcat(reply, e.text ? e.text : "", sizeof(reply));
                if (!e.more) agent_publish(AGENT_SPEAKING, NULL, reply, NULL);
                break;
            case EV_SPEECH:
                if (e.pcm && !*cancelled) agent_play(e.pcm, e.samples, e.rate);
                break;
            case EV_DONE:
                agent_publish(AGENT_IDLE, NULL, NULL, NULL);
                finished = true;
                break;
            case EV_ERROR:
                agent_publish(AGENT_ERROR, NULL, NULL, e.text && e.text[0] ? e.text : "The phone could not answer");
                finished = true;
                break;
            }
            event_free(&e);
        }
    }
    asking = false;
    events_flush();
    return true;
}

void link_agent_cancel(void)
{
    agent_cancel = true;
}

/* ------------------------------------------------------------------ state */

bool link_app(void)
{
    return app;
}

bool link_can_agent(void)
{
    return app && can_agent;
}

/* The phone went away (called on Bluetooth's task). */
void link_reset(void)
{
    bool was = app;
    app = false, can_agent = false; /* also ends a question that is waiting */
    hello_due = false;
    phone_caps = 0;
    rx_drop(&art_rx);
    rx_drop(&photo_rx);
    free(speech);
    speech = NULL;
    if (was) ui_post(art_drop, NULL);
}
