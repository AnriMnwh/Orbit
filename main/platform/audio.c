/* Speaker (ES8311) and microphones (ES7210).
 *
 * Both chips share one I2S bus, so they are always opened together with the
 * same format: 16-bit stereo at the requested rate. Mono audio is doubled on
 * the way out and averaged on the way in.
 */
#include <math.h>
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
static int16_t *rec_buf;
static size_t rec_len;

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
        static int16_t stereo[CHUNK * 2];
        for (size_t off = 0; off < samples && !stop_flag; off += CHUNK) {
            size_t n = samples - off < CHUNK ? samples - off : CHUNK;
            for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = pcm[off + i];
            level = peak_of(pcm + off, n, 4);
            esp_codec_dev_write(spk, stereo, (int)(n * 4));
        }
        bus_close();
    }
    xSemaphoreGive(bus_lock);
}

/* ---------------------------------------------------------------- record */

static void record_task(void *arg)
{
    LV_UNUSED(arg);
    static int16_t stereo[CHUNK * 2];
    const size_t cap = REC_RATE * REC_MAX_SECONDS;
    xSemaphoreTake(bus_lock, portMAX_DELAY);
    if (bus_open(REC_RATE)) {
        while (recording && rec_len + CHUNK <= cap) {
            if (esp_codec_dev_read(mic, stereo, sizeof(stereo)) != ESP_CODEC_DEV_OK) break;
            for (size_t i = 0; i < CHUNK; i++) rec_buf[rec_len + i] = (int16_t)((stereo[2 * i] + stereo[2 * i + 1]) / 2);
            level = peak_of(rec_buf + rec_len, CHUNK, 4);
            rec_len += CHUNK;
        }
        bus_close();
    }
    recording = false;
    xSemaphoreGive(bus_lock);
    xSemaphoreGive(rec_done);
    vTaskDelete(NULL);
}

bool audio_record_start(void)
{
    if (!mic || !bus_lock || recording) return false;
    rec_buf = heap_caps_malloc(REC_RATE * REC_MAX_SECONDS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!rec_buf) return false;
    rec_len = 0;
    stop_flag = true; /* cut off anything still playing */
    recording = true;
    xSemaphoreTake(rec_done, 0);
    if (xTaskCreate(record_task, "record", 4096, NULL, 6, NULL) != pdPASS) {
        recording = false;
        free(rec_buf);
        rec_buf = NULL;
        return false;
    }
    return true;
}

int16_t *audio_record_stop(size_t *samples)
{
    *samples = 0;
    if (!rec_buf) return NULL;
    recording = false;
    xSemaphoreTake(rec_done, pdMS_TO_TICKS(2000));
    int16_t *out = rec_buf;
    *samples = rec_len;
    rec_buf = NULL;
    return out;
}

/* ----------------------------------------------------------- UI sounds */

typedef struct {
    uint16_t hz, ms;
} tone_t;

static void play_tones(const tone_t *t, int n)
{
    static int16_t buf[REC_RATE / 4];
    static int16_t stereo[CHUNK * 2];
    if (!bus_lock || recording) return;
    xSemaphoreTake(bus_lock, portMAX_DELAY);
    stop_flag = false;
    /* Open the codec and PA once for the whole tone sequence so multi-beep
     * alerts and alarms do not pop or clip between notes. */
    if (bus_open(REC_RATE)) {
        for (int k = 0; k < n && !stop_flag; k++) {
            size_t len = (size_t)REC_RATE * t[k].ms / 1000;
            if (len > sizeof(buf) / sizeof(buf[0])) len = sizeof(buf) / sizeof(buf[0]);
            for (size_t i = 0; i < len; i++) {
                /* Quick fade in and out so tones do not click. */
                float env = 1.0f;
                if (i < 80) env = (float)i / 80.0f;
                if (len - i < 240) env = (float)(len - i) / 240.0f;
                buf[i] = t[k].hz ? (int16_t)(9000.0f * env * sinf(6.2831853f * t[k].hz * (float)i / REC_RATE)) : 0;
            }
            for (size_t off = 0; off < len && !stop_flag; off += CHUNK) {
                size_t chunk = len - off < CHUNK ? len - off : CHUNK;
                for (size_t i = 0; i < chunk; i++) stereo[2 * i] = stereo[2 * i + 1] = buf[off + i];
                esp_codec_dev_write(spk, stereo, (int)(chunk * 4));
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
