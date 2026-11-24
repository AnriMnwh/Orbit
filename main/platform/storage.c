/* Flash storage (FAT on the "data" partition) and the photo library. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "platform.h"

static const char *TAG = "storage";

#define PHOTO_MAX 64
#define JPEG_MAX_BYTES (400 * 1024)

static wl_handle_t wl = WL_INVALID_HANDLE;
static bool mounted;
static SemaphoreHandle_t lock; /* guards the photo list below */

void storage_init(void)
{
    lock = xSemaphoreCreateMutex();
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = 6,
        .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(DATA_DIR, "data", &cfg, &wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage did not mount: %s", esp_err_to_name(err));
        return;
    }
    mounted = true;
    mkdir(PHOTO_DIR, 0775);
    mkdir(TILE_DIR, 0775);
}

void svc_storage_info(uint32_t *total_kb, uint32_t *free_kb)
{
    uint64_t total = 0, free_bytes = 0;
    if (mounted) esp_vfs_fat_info(DATA_DIR, &total, &free_bytes);
    *total_kb = (uint32_t)(total / 1024);
    *free_kb = (uint32_t)(free_bytes / 1024);
}

static void empty_dir(const char *dir)
{
    DIR *d = mounted ? opendir(dir) : NULL;
    if (!d) return;
    struct dirent *e;
    char path[320];
    while ((e = readdir(d))) {
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        unlink(path);
    }
    closedir(d);
}

void svc_clear(int what)
{
    if (what == CLEAR_PHOTOS) {
        empty_dir(PHOTO_DIR);
        photos_changed();
    } else if (what == CLEAR_MAPS) {
        empty_dir(TILE_DIR);
    }
}

/* ----------------------------------------------------------------- photos */

/* File names, sorted, guarded by a mutex because the web server lists them
 * from its own task while the UI reads them from the LVGL task. The mutex is
 * created in storage_init(), before either task can ask. */
static char names[PHOTO_MAX][24];
static int count = -1; /* -1: not scanned yet */

static uint16_t *pixels; /* one decoded photo, 466 x 466 RGB565 */
static lv_image_dsc_t dsc;
static int decoded = -1;

static int by_name(const void *a, const void *b)
{
    return strcmp(a, b);
}

static void scan(void)
{
    count = 0;
    DIR *d = mounted ? opendir(PHOTO_DIR) : NULL;
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && count < PHOTO_MAX) {
        size_t n = strlen(e->d_name);
        if (n < 5 || n >= sizeof(names[0]) || strcasecmp(e->d_name + n - 4, ".jpg") != 0) continue;
        strcpy(names[count++], e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(names[0]), by_name);
}

static void take(void)
{
    /* the mutex is created in storage_init(), before any task can reach this */
    xSemaphoreTake(lock, portMAX_DELAY);
    if (count < 0) scan();
}

int photos_list(char out[][24], int max)
{
    take();
    int n = count < max ? count : max;
    memcpy(out, names, (size_t)n * sizeof(names[0]));
    xSemaphoreGive(lock);
    return n;
}

static void photos_apply(void *arg)
{
    LV_UNUSED(arg);
    decoded = -1;
    bus_emit(EVT_PHOTOS, NULL);
}

void photos_changed(void)
{
    take();
    scan();
    xSemaphoreGive(lock);
    ui_post(photos_apply, NULL);
}

int svc_photo_count(void)
{
    take();
    int n = count;
    xSemaphoreGive(lock);
    return n;
}

/* Decodes a JPEG that must be exactly w x h into `out`, which must come from
 * jpeg_calloc_align(). Shared by photos and album art. */
bool jpeg_to_rgb565(const uint8_t *jpg, size_t len, int w, int h, uint16_t *out)
{
    jpeg_dec_handle_t dec = NULL;
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;
    jpeg_dec_io_t io = {.inbuf = (uint8_t *)jpg, .inbuf_len = (int)len};
    jpeg_dec_header_info_t info;
    bool ok = jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK && info.width == w && info.height == h;
    if (ok) {
        io.outbuf = (uint8_t *)out;
        ok = jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
    }
    jpeg_dec_close(dec);
    return ok;
}

/* True if the file is a JPEG this watch can show: baseline, exactly w x h. */
static bool jpeg_fits(const uint8_t *jpg, size_t len, int w, int h)
{
    if (len < 200 || jpg[0] != 0xFF || jpg[1] != 0xD8) return false;
    jpeg_dec_handle_t dec = NULL;
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;
    jpeg_dec_io_t io = {.inbuf = (uint8_t *)jpg, .inbuf_len = (int)len};
    jpeg_dec_header_info_t info;
    bool ok = jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK && info.width == w && info.height == h;
    jpeg_dec_close(dec);
    return ok;
}

/* The same check for a photo already written to flash (the browser upload
 * streams to a file). The file is read into external RAM, so a 400 KB photo
 * does not touch the scarce internal heap. */
bool photo_file_fits(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (size >= 200 && size <= JPEG_MAX_BYTES) {
        uint8_t *buf = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
        if (buf) {
            ok = fread(buf, 1, (size_t)size, f) == (size_t)size && jpeg_fits(buf, (size_t)size, SCR, SCR);
            free(buf);
        }
    }
    fclose(f);
    return ok;
}

/* The path and name for the next photo: one past the highest "pNNNN.jpg".
 * False when the library is full. */
bool photos_new_path(char *path, size_t len, char name[24])
{
    take();
    int next = 1, n = count;
    for (int i = 0; i < count; i++) {
        int v = atoi(names[i] + 1);
        if (names[i][0] == 'p' && v >= next) next = v + 1;
    }
    xSemaphoreGive(lock);
    if (!mounted || n >= PHOTO_LIMIT) return false;
    snprintf(name, 24, "p%04d.jpg", next);
    snprintf(path, len, PHOTO_DIR "/%s", name);
    return true;
}

/* Adds a 466 x 466 JPEG held in memory to the library and reports the name
 * it was given. Returns NULL, or a one-word reason it was refused. It writes
 * to flash, so never call it on the LVGL thread or a task whose stack is in
 * the external RAM. */
const char *photo_add(const uint8_t *jpg, size_t len, char name[24])
{
    char path[64];
    uint64_t total = 0, free_bytes = 0;
    if (!mounted) return "storage";
    if (!jpeg_fits(jpg, len, SCR, SCR)) return "bad";
    if (!photos_new_path(path, sizeof(path), name)) return "full";
    if (esp_vfs_fat_info(DATA_DIR, &total, &free_bytes) != ESP_OK || free_bytes < len + 32 * 1024) return "space";

    FILE *f = fopen(path, "wb");
    if (!f) return "storage";
    bool ok = fwrite(jpg, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        unlink(path);
        return "storage";
    }
    photos_changed();
    return NULL;
}

/* Removes one photo by the name photo_add() or the setup page gave it. */
bool photo_remove(const char *name)
{
    size_t n = strlen(name);
    if (!mounted || n < 5 || n >= 24 || strcasecmp(name + n - 4, ".jpg") != 0) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool plain = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
        if (!plain && !(c == '.' && i == n - 4)) return false;
    }
    char path[64];
    snprintf(path, sizeof(path), PHOTO_DIR "/%s", name);
    bool gone = unlink(path) == 0;
    if (gone) photos_changed();
    return gone;
}

static bool decode(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *jpg = size > 0 && size <= JPEG_MAX_BYTES ? malloc((size_t)size) : NULL;
    bool ok = jpg && fread(jpg, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    ok = ok && jpeg_to_rgb565(jpg, (size_t)size, SCR, SCR, pixels);
    free(jpg);
    if (!ok) ESP_LOGW(TAG, "could not decode %s", path);
    return ok;
}

const lv_image_dsc_t *svc_photo(int index)
{
    char path[64];
    take();
    bool valid = index >= 0 && index < count;
    if (valid) snprintf(path, sizeof(path), PHOTO_DIR "/%s", names[index]);
    xSemaphoreGive(lock);
    if (!valid) return NULL;

    if (!pixels) pixels = jpeg_calloc_align(SCR * SCR * 2, 16);
    if (!pixels) return NULL;
    if (decoded != index) {
        decoded = -1;
        if (!decode(path)) return NULL;
        decoded = index;
    }
    dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)pixels,
    };
    return &dsc;
}
