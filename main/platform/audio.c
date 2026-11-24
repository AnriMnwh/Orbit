/* Speaker (ES8311) and microphones (ES7210).
 *
 * Both chips share one I2S bus, so they are always opened together with the
 * same format: 16-bit stereo at the requested rate. Mono audio is doubled on
 * the way out and averaged on the way in.
 */
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "platform.h"

static const char *TAG = "audio";

#define REC_RATE 16000
#define REC_MAX_SECONDS 20
#define CHUNK 512

static esp_codec_dev_handle_t spk, mic;
static SemaphoreHandle_t bus_lock;  /* one user of the audio bus at a time */
static SemaphoreHandle_t rec_done;
static QueueHandle_t sounds;
static volatile bool stop_flag, recording;
static volatile float level;
static int16_t bus_stereo[CHUNK * 2]; /* shared under bus_lock to save internal BSS */

float audio_level(void)
{
    return level;
}

static bool bus_open(int rate)
{
    if (!spk || !mic) return false;
    esp_codec_dev_sample_info_t fs = {.sample_rate = (uint32_t)rate, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(spk, &fs) != ESP_CODEC_DEV_OK) return false;
    if (esp_codec_dev_open(mic, &fs) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(spk);
        return false;
    }
    esp_codec_dev_set_out_vol(spk, CFG.volume); /* Settings > Sound & alerts */
    esp_codec_dev_set_in_gain(mic, 30.0f);
    return true;
}

static void bus_close(void)
{
    esp_codec_dev_close(spk);
    esp_codec_dev_close(mic);
    level = 0;
}

static float peak_of(const int16_t *s, size_t n, size_t step)
{
    int peak = 0;
    for (size_t i = 0; i < n; i += step) {
        int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak) peak = v;
    }
    float f = (float)peak / 12000.0f;
    return f > 1 ? 1 : f;
}

void audio_stop(void)
{
    stop_flag = true;
}

void audio_play(const int16_t *pcm, size_t samples, int rate)
{
    if (!bus_lock || recording) return;
    xSemaphoreTake(bus_lock, portMAX_DELAY);
    stop_flag = false;
    if (bus_open(rate)) {
        for (size_t off = 0; off < samples && !stop_flag; off += CHUNK) {
            size_t n = samples - off < CHUNK ? samples - off : CHUNK;
            for (size_t i = 0; i < n; i++) bus_stereo[2 * i] = bus_stereo[2 * i + 1] = pcm[off + i];
            level = peak_of(pcm + off, n, 4);
            esp_codec_dev_write(spk, bus_stereo, (int)(n * 4));
        }
        bus_close();
    }
    xSemaphoreGive(bus_lock);
}

/* ---------------------------------------------------------------- record */

/* One recording. The record task and audio_record_stop() share it: whichever
 * finishes last frees it, so a stop that times out (the task still blocked
 * reading the microphone) can never leave the task writing into freed memory. */
enum { REC_RUNNING, REC_DONE, REC_ABANDONED };

typedef struct {
    int16_t *buf;
    size_t len;
    volatile bool stop;
    atomic_int state;
} rec_t;

static rec_t *rec; /* the recording audio_record_stop() will collect */

static void record_task(void *arg)
{
    rec_t *r = arg;
    const size_t cap = REC_RATE * REC_MAX_SECONDS;
    xSemaphoreTake(bus_lock, portMAX_DELAY);
    if (bus_open(REC_RATE)) {
        while (!r->stop && r->len + CHUNK <= cap) {
            if (esp_codec_dev_read(mic, bus_stereo, sizeof(bus_stereo)) != ESP_CODEC_DEV_OK) break;
            for (size_t i = 0; i < CHUNK; i++) r->buf[r->len + i] = (int16_t)((bus_stereo[2 * i] + bus_stereo[2 * i + 1]) / 2);
            level = peak_of(r->buf + r->len, CHUNK, 4);
            r->len += CHUNK;
        }
        bus_close();
    }
    if (rec == r) recording = false; /* ran out of room or the codec failed */
    xSemaphoreGive(bus_lock);
    int before = atomic_exchange(&r->state, REC_DONE);
    if (before == REC_ABANDONED) {
        free(r->buf);
        free(r);
    } else {
        xSemaphoreGive(rec_done);
    }
    vTaskDelete(NULL);
}

bool audio_record_start(void)
{
    if (!mic || !bus_lock || recording) return false;
    rec_t *r = calloc(1, sizeof(*r));
    if (!r) return false;
    r->buf = heap_caps_malloc(REC_RATE * REC_MAX_SECONDS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!r->buf) {
        free(r);
        return false;
    }
    stop_flag = true; /* cut off anything still playing */
    rec = r;
    recording = true;
    xSemaphoreTake(rec_done, 0);
    if (xTaskCreate(record_task, "record", 4096, r, 6, NULL) != pdPASS) {
        recording = false;
        rec = NULL;
        free(r->buf);
        free(r);
        return false;
    }
    return true;
}

/* Returns the recording (caller frees), or NULL with *samples = 0 if there is
 * none or the microphone did not let go in time. */
int16_t *audio_record_stop(size_t *samples)
{
    *samples = 0;
    rec_t *r = rec;
    if (!r) return NULL;
    rec = NULL;
    r->stop = true;
    recording = false;
    if (xSemaphoreTake(rec_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        int expected = REC_RUNNING;
        if (atomic_compare_exchange_strong(&r->state, &expected, REC_ABANDONED)) {
            return NULL; /* the task still owns it and frees it when it ends */
        }
        /* It finished just now; its give is on the way. */
        xSemaphoreTake(rec_done, pdMS_TO_TICKS(1000));
    }
    int16_t *out = r->buf;
    *samples = r->len;
    free(r);
    return out;
}

/* ----------------------------------------------------------- UI sounds */

typedef struct {
    uint16_t hz, ms;
} tone_t;

static void play_tones(const tone_t *t, int n)
{
    if (!bus_lock || recording) return;
    xSemaphoreTake(bus_lock, portMAX_DELAY);
    stop_flag = false;
    /* Open the codec and PA once for the whole tone sequence so multi-beep
     * alerts and alarms do not pop or clip between notes. */
    if (bus_open(REC_RATE)) {
        for (int k = 0; k < n && !stop_flag; k++) {
            size_t len = (size_t)REC_RATE * t[k].ms / 1000;
            if (len > REC_RATE / 4) len = REC_RATE / 4;
            /* Each chunk is computed straight into the shared stereo buffer, so no
             * tone-sized buffer sits in internal RAM. */
            for (size_t off = 0; off < len && !stop_flag; off += CHUNK) {
                size_t chunk = len - off < CHUNK ? len - off : CHUNK;
                for (size_t j = 0; j < chunk; j++) {
                    size_t i = off + j;
                    /* Quick fade in and out so tones do not click. */
                    float env = 1.0f;
                    if (i < 80) env = (float)i / 80.0f;
                    if (len - i < 240) env = (float)(len - i) / 240.0f;
                    int16_t v = t[k].hz ? (int16_t)(9000.0f * env * sinf(6.2831853f * t[k].hz * (float)i / REC_RATE)) : 0;
                    bus_stereo[2 * j] = bus_stereo[2 * j + 1] = v;
                }
                esp_codec_dev_write(spk, bus_stereo, (int)(chunk * 4));
            }
        }
        bus_close();
    }
    xSemaphoreGive(bus_lock);
}

static void sound_task(void *arg)
{
    LV_UNUSED(arg);
    static const tone_t tap[] = {{1900, 14}};
    static const tone_t notify[] = {{880, 70}, {1320, 110}};
    static const tone_t alarm[] = {{1568, 130}, {0, 70}, {1568, 130}, {0, 70}, {1568, 130}, {0, 220},
                                   {1568, 130}, {0, 70}, {1568, 130}, {0, 70}, {1568, 130}};
    static const tone_t good[] = {{1047, 60}, {1568, 100}};
    static const tone_t bad[] = {{392, 90}, {294, 150}};
    for (;;) {
        int id;
        if (xQueueReceive(sounds, &id, portMAX_DELAY) != pdTRUE) continue;
        switch (id) {
        case SND_TAP: play_tones(tap, 1); break;
        case SND_NOTIFY: play_tones(notify, 2); break;
        case SND_ALARM: play_tones(alarm, 11); break;
        case SND_OK: play_tones(good, 2); break;
        case SND_ERROR: play_tones(bad, 2); break;
        }
    }
}

void svc_sound(int id)
{
    if (sounds && !recording) xQueueSend(sounds, &id, 0);
}

void audio_init(void)
{
    spk = bsp_audio_codec_speaker_init();
    mic = bsp_audio_codec_microphone_init();
    if (!spk || !mic) {
        ESP_LOGW(TAG, "audio chips did not start: sounds and the assistant are off");
        return;
    }
    bus_lock = xSemaphoreCreateMutex();
    rec_done = xSemaphoreCreateBinary();
    sounds = xQueueCreate(4, sizeof(int));
    /* Stacks in external RAM: these tasks never touch flash, and internal RAM is scarce. */
    xTaskCreateWithCaps(sound_task, "sound", 4096, NULL, 4, NULL, MALLOC_CAP_SPIRAM);
}
