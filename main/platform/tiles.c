/* Map tiles: fetched over Wi-Fi, kept on flash, decoded on demand.
 *
 * Tiles come from OpenStreetMap's standard server by default. They are
 * stored as the original PNG and recoloured for a dark screen when decoded.
 * The decoded-tile cache is only ever touched on the LVGL thread.
 */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "src/libs/lodepng/lodepng.h"

#include "http.h"
#include "platform.h"

static const char *TAG = "tiles";

#define TILE 256
#define CACHE 10
#define PENDING 12
#define DEFAULT_URL "https://tile.openstreetmap.org/%d/%d/%d.png"
#define MIN_FREE_KB 700

typedef struct {
    int z, x, y;
} tkey_t;

static struct {
    tkey_t k;
    uint16_t *px;
    lv_image_dsc_t dsc;
    uint32_t used;
} cache[CACHE];

static struct {
    tkey_t k;
    bool active;
    uint32_t retry_after; /* non-zero: failed, do not ask again before this */
} pending[PENDING];

static QueueHandle_t requests;
static uint32_t stamp;

/* Offline download job, walked one tile at a time by the worker. */
static struct {
    double lat, lon;
    volatile int next, total, done;
} job;

static bool same(tkey_t a, tkey_t b)
{
    return a.z == b.z && a.x == b.x && a.y == b.y;
}

/* ----------------------------------------------------------------- worker */

static void tile_path(tkey_t k, char *out, size_t len)
{
    snprintf(out, len, TILE_DIR "/%d_%d_%d.png", k.z, k.x, k.y);
}

static void make_room(void)
{
    uint32_t total, free_kb;
    svc_storage_info(&total, &free_kb);
    if (free_kb > MIN_FREE_KB) return;
    DIR *d = opendir(TILE_DIR);
    if (!d) return;
    struct dirent *e;
    char path[320];
    for (int n = 0; n < 24 && (e = readdir(d)); n++) {
        snprintf(path, sizeof(path), TILE_DIR "/%s", e->d_name);
        unlink(path);
    }
    closedir(d);
}

static bool is_png(const char *data, size_t len)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return len > 64 && memcmp(data, sig, 8) == 0;
}

/* Returns PNG bytes from flash or the network (caller frees), or NULL. */
static char *fetch(tkey_t k, size_t *len, bool *downloaded)
{
    char path[64];
    tile_path(k, path, sizeof(path));
    *downloaded = false;

    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = size > 64 && size < 200000 ? heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM) : NULL;
        bool ok = buf && fread(buf, 1, (size_t)size, f) == (size_t)size && is_png(buf, (size_t)size);
        fclose(f);
        if (ok) {
            *len = (size_t)size;
            return buf;
        }
        free(buf);
        unlink(path); /* damaged file: fetch it again */
    }
    if (!net_online()) return NULL;

    /* A custom tile address is used as a format string, so it must contain
     * exactly three "%d" (zoom, x, y) and no other "%". */
    char pattern[200], url[260];
    bool custom = net_secret_get("tile_url", pattern, sizeof(pattern));
    int marks = 0;
    for (const char *p = pattern; custom && *p; p++) {
        if (*p != '%') continue;
        if (p[1] != 'd') marks = 99;
        marks++;
    }
    if (!custom || marks != 3) strlcpy(pattern, DEFAULT_URL, sizeof(pattern));
    snprintf(url, sizeof(url), pattern, k.z, k.x, k.y);
    http_request_t q = {.url = url, .timeout_ms = 12000};
    http_reply_t r = {.max = 200000};
    int status = http_request(&q, &r);
    if (status != 200 || !is_png(r.data, r.len)) {
        ESP_LOGW(TAG, "tile %d/%d/%d: HTTP %d", k.z, k.x, k.y, status);
        http_free(&r);
        return NULL;
    }
    make_room();
    f = fopen(path, "wb");
    if (f) {
        fwrite(r.data, 1, r.len, f);
        fclose(f);
    }
    *len = r.len;
    *downloaded = true;
    return r.data;
}

/* PNG -> RGB565, flipped to a dark palette: lightness is mirrored while hue
 * and saturation stay, so parks remain green and water stays blue. */
static uint16_t *decode(const char *png, size_t len)
{
    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&rgba, &w, &h, (const unsigned char *)png, len) != 0 || w != TILE || h != TILE) {
        if (rgba) lv_free(rgba);
        return NULL;
    }
    uint16_t *px = heap_caps_malloc(TILE * TILE * 2, MALLOC_CAP_SPIRAM);
    if (px) {
        const unsigned char *s = rgba;
        for (int i = 0; i < TILE * TILE; i++, s += 4) {
            int r = s[0], g = s[1], b = s[2];
            int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            int shift = 255 - mx - mn;
            r += shift, g += shift, b += shift;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            px[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    lv_free(rgba);
    return px;
}

typedef struct {
    tkey_t k;
    uint16_t *px;
} result_t;

/* LVGL thread: put a finished tile into the cache, or note the failure. */
static void deliver(void *arg)
{
    result_t *r = arg;
    for (int i = 0; i < PENDING; i++) {
        if (!pending[i].active || !same(pending[i].k, r->k)) continue;
        if (r->px) pending[i].active = false;
        else pending[i].retry_after = svc_millis() + 20000;
    }
    if (r->px) {
        int slot = 0;
        for (int i = 1; i < CACHE; i++) {
            if (cache[i].used < cache[slot].used) slot = i;
        }
        /* LVGL's own image cache is off (the default), so nothing else holds this. */
        free(cache[slot].px);
        cache[slot].k = r->k, cache[slot].px = r->px, cache[slot].used = ++stamp;
        cache[slot].dsc = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = TILE, .h = TILE, .stride = TILE * 2},
            .data_size = TILE * TILE * 2,
            .data = (const uint8_t *)r->px,
        };
        bus_emit(EVT_TILE, NULL);
    }
    free(r);
}

/* The n-th tile of the offline job: zooms 12..16 around the chosen point. */
static bool job_tile(int n, tkey_t *out)
{
    for (int z = 12; z <= 16; z++) {
        int radius = z >= 14 ? 2 : 1, side = 2 * radius + 1;
        if (n >= side * side) {
            n -= side * side;
            continue;
        }
        double scale = (double)(1 << z), rad = job.lat * 3.14159265358979 / 180.0;
        int cx = (int)((job.lon + 180.0) / 360.0 * scale);
        int cy = (int)((1.0 - log(tan(rad) + 1.0 / cos(rad)) / 3.14159265358979) / 2.0 * scale);
        int span = 1 << z;
        out->z = z;
        out->x = ((cx - radius + n % side) % span + span) % span;
        out->y = cy - radius + n / side;
        return out->y >= 0 && out->y < span;
    }
    return false;
}

static void worker(void *arg)
{
    LV_UNUSED(arg);
    for (;;) {
        tkey_t k;
        bool downloaded = false;
        size_t len = 0;
        if (xQueueReceive(requests, &k, pdMS_TO_TICKS(400)) == pdTRUE) {
            char *png = fetch(k, &len, &downloaded);
            result_t *r = calloc(1, sizeof(*r));
            if (r) {
                r->k = k;
                r->px = png ? decode(png, len) : NULL;
                ui_post(deliver, r);
            }
            free(png);
        } else if (job.next < job.total) {
            /* Nothing on screen needs a tile: carry on with the offline job. */
            if (job_tile(job.next, &k)) {
                char *png = fetch(k, &len, &downloaded);
                free(png);
            }
            job.next++, job.done++;
        }
        if (downloaded) vTaskDelay(pdMS_TO_TICKS(250)); /* be gentle with the tile server */
    }
}

/* -------------------------------------------------------------------- API */

/* The worker is created the first time maps are wanted, not at boot. */
static void worker_start(void)
{
    static bool started;
    if (!started && requests) started = xTaskCreate(worker, "tiles", 8192, NULL, 3, NULL) == pdPASS;
}

const lv_image_dsc_t *svc_tile(int z, int x, int y)
{
    tkey_t k = {z, x, y};
    for (int i = 0; i < CACHE; i++) {
        if (cache[i].px && same(cache[i].k, k)) {
            cache[i].used = ++stamp;
            return &cache[i].dsc;
        }
    }
    if (!requests) return NULL;
    worker_start();
    int free_slot = -1;
    uint32_t now = svc_millis();
    for (int i = 0; i < PENDING; i++) {
        if (pending[i].active && same(pending[i].k, k)) {
            if (!pending[i].retry_after || (int32_t)(now - pending[i].retry_after) < 0) return NULL;
            pending[i].active = false; /* failed a while ago: try again */
        }
        if (!pending[i].active && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0 && xQueueSend(requests, &k, 0) == pdTRUE) {
        pending[free_slot].k = k, pending[free_slot].active = true, pending[free_slot].retry_after = 0;
    }
    return NULL;
}

int tiles_preload(double lat, double lon)
{
    job.lat = lat, job.lon = lon;
    job.next = 0, job.done = 0;
    job.total = 9 + 9 + 25 + 25 + 25;
    worker_start();
    return job.total;
}

void tiles_progress(int *done, int *total)
{
    *done = job.done, *total = job.total;
}

void tiles_init(void)
{
    requests = xQueueCreate(PENDING, sizeof(tkey_t));
}
