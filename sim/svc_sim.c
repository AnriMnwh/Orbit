/* Stand-ins for the watch hardware, with believable sample data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

/* 2026-11-10 10:09:36 UTC, a Tuesday. */
#define SIM_EPOCH 1794305376

uint32_t svc_millis(void)
{
    return sim_now_ms;
}

time_t svc_time(void)
{
    return (time_t)SIM_EPOCH + sim_now_ms / 1000;
}

void svc_localtime(struct tm *out)
{
    time_t t = svc_time() + CFG.tz_minutes * 60;
    *out = *gmtime(&t);
}

bool svc_time_valid(void)
{
    return true;
}

bool svc_kv_load(const char *key, void *buf, size_t len)
{
    return false;
}

void svc_kv_save(const char *key, const void *buf, size_t len)
{
}

void svc_brightness(int percent)
{
}

void svc_sound(int id)
{
}

void svc_power_off(void)
{
    puts("[sim] power off");
}

void svc_restart(void)
{
    puts("[sim] restart");
}

void svc_charge_limit(bool on)
{
    LV_UNUSED(on);
}

void svc_storage_info(uint32_t *total_kb, uint32_t *free_kb)
{
    *total_kb = 10176, *free_kb = 8420;
}

int svc_update_allow(void)
{
    return 300;
}

void svc_set_time(time_t utc)
{
}

void svc_factory_reset(void)
{
    puts("[sim] factory reset");
}

void svc_clear(int what)
{
}

bool svc_configured(int what)
{
    return what != HAS_CAMERA;
}

void svc_wifi_scan(void)
{
    static const wifi_net_t found[] = {
        {"Home", -48, true}, {"Home-Guest", -55, false}, {"Studio 2.4", -67, true},
        {"CoffeeCorner", -74, true}, {"NETGEAR-41", -83, true},
    };
    memcpy(S.scan, found, sizeof(found));
    S.scan_count = 5, S.scanning = false;
    bus_emit(EVT_WIFI, NULL);
}

void svc_wifi_join(const char *ssid, const char *password)
{
    S.wifi.joining = true, S.wifi.failed = false, S.wifi.connected = false;
    bus_emit(EVT_WIFI, NULL);
}

void svc_wifi_forget(void)
{
    S.wifi.connected = false;
    bus_emit(EVT_WIFI, NULL);
}

void svc_memory_info(uint32_t *internal_kb, uint32_t *external_kb)
{
    *internal_kb = 142, *external_kb = 6480;
}

void svc_link_keys_mode(bool on)
{
    S.link.keys_mode = on;
    S.link.status = LINK_CONNECTED;
    S.link.peer = on ? PEER_KEYS : PEER_IPHONE;
    snprintf(S.link.name, sizeof(S.link.name), "%s", on ? "DESKTOP-ORBIT" : "iPhone");
    bus_emit(EVT_LINK, NULL);
}

void svc_link_midi_mode(bool on)
{
    S.link.midi_mode = on;
    bus_emit(EVT_LINK, NULL);
}

void svc_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
}

void svc_notif_reply(uint32_t id, const char *text)
{
    printf("[sim] reply to %u: %s\n", (unsigned)id, text);
}

void svc_tilt(float *x, float *y, float *z)
{
    *x = 0.12f, *y = -0.05f, *z = 0.99f;
}

void svc_synth(bool on)
{
}

void svc_tuner(bool on)
{
    S.tuner.valid = on;
    S.tuner.hz = 438.2f;
    bus_emit(EVT_TUNER, NULL);
}

void svc_link_forget(void)
{
    S.link.status = LINK_WAITING;
    S.link.name[0] = 0;
    bus_emit(EVT_LINK, NULL);
}

void svc_media(media_cmd_t cmd)
{
    if (cmd == MEDIA_TOGGLE) S.media.playing = !S.media.playing;
    if (cmd == MEDIA_SHUFFLE) S.media.shuffle = !S.media.shuffle;
    if (cmd == MEDIA_REPEAT) S.media.repeat = !S.media.repeat;
    if (cmd == MEDIA_VOL_UP && S.media.volume < 100) S.media.volume += 6;
    if (cmd == MEDIA_VOL_DOWN && S.media.volume > 0) S.media.volume -= 6;
    S.media.stamp_ms = sim_now_ms;
    bus_emit(EVT_MEDIA, NULL);
}

void svc_media_seek(int seconds)
{
    S.media.position_s = seconds, S.media.stamp_ms = sim_now_ms;
    bus_emit(EVT_MEDIA, NULL);
}

/* Stands in for a track's picture: one of the sample photos, darkened the way
 * the watch darkens real album art. */
const lv_image_dsc_t *svc_album_art(void)
{
    static uint16_t *px;
    static lv_image_dsc_t dsc;
    char path[512];
    snprintf(path, sizeof(path), "%s/photo_2.rgb565", sim_data_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (!px) px = malloc(SCR * SCR * 2);
    size_t n = fread(px, 2, SCR * SCR, f);
    fclose(f);
    if (n != SCR * SCR) return NULL;
    for (int i = 0; i < SCR * SCR; i++) {
        int r = (px[i] >> 11) * 102 >> 8, g = ((px[i] >> 5) & 63) * 102 >> 8, b = (px[i] & 31) * 102 >> 8;
        px[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)px,
    };
    return &dsc;
}

void svc_hid_keys(uint8_t mods, uint8_t key)
{
    printf("[sim] keys mods=%02x key=%02x\n", mods, key);
}

void svc_hid_text(const char *text)
{
    printf("[sim] type \"%s\"\n", text);
}

void svc_hid_consumer(uint16_t usage)
{
    printf("[sim] consumer %04x\n", usage);
}

void svc_find_phone(bool on)
{
}

void svc_gps_want(bool on)
{
}

/* No phone here: the scenarios fill S.busy and S.day themselves. */
bool svc_calendar_month(int year, int month)
{
    return false;
}

bool svc_calendar_day(int year, int month, int day)
{
    return false;
}

void svc_notif_dismiss(uint32_t id)
{
}

void svc_call(bool accept)
{
    S.call.active = false;
    bus_emit(EVT_CALL, NULL);
}

void svc_wifi_setup_mode(bool on)
{
    S.wifi.setup_mode = on;
    snprintf(S.wifi.setup_pass, sizeof(S.wifi.setup_pass), "48213907");
    bus_emit(EVT_WIFI, NULL);
}

/* --- photos: raw 466x466 RGB565 files written by tools/sim_shots.py --- */

#define PHOTO_COUNT 3
static uint16_t *photo_buf;
static lv_image_dsc_t photo_dsc;

int svc_photo_count(void)
{
    return PHOTO_COUNT;
}

const lv_image_dsc_t *svc_photo(int index)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/photo_%d.rgb565", sim_data_dir, index % PHOTO_COUNT);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (!photo_buf) photo_buf = malloc(SCR * SCR * 2);
    size_t n = fread(photo_buf, 2, SCR * SCR, f);
    fclose(f);
    if (n != SCR * SCR) return NULL;
    photo_dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)photo_buf,
    };
    return &photo_dsc;
}

/* --- map tiles: a procedural stand-in that looks enough like a street map --- */

#define TILE 256
#define TILE_CACHE 12
static struct {
    int z, x, y;
    uint16_t *px;
    lv_image_dsc_t dsc;
} tiles[TILE_CACHE];
static int tile_next;

static uint32_t hash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 13);
}

static uint16_t rgb(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void tile_paint(uint16_t *px, int z, int tx, int ty)
{
    for (int y = 0; y < TILE; y++) {
        for (int x = 0; x < TILE; x++) {
            int gx = tx * TILE + x, gy = ty * TILE + y;
            int bx = gx / 64, by = gy / 64;
            uint32_t h = hash((uint32_t)bx + (uint32_t)z * 977u, (uint32_t)by);
            uint16_t c = rgb(24, 27, 33);
            if (h % 11 == 0) c = rgb(22, 44, 34);       /* park */
            else if (h % 17 == 0) c = rgb(18, 34, 58);  /* water */
            int rx = gx % 64, ry = gy % 64;
            if (rx < 5 || ry < 5) c = rgb(58, 62, 72);  /* street */
            if (gx % 256 < 9 || gy % 256 < 9) c = rgb(104, 96, 78); /* main road */
            px[y * TILE + x] = c;
        }
    }
}

const lv_image_dsc_t *svc_tile(int z, int x, int y)
{
    for (int i = 0; i < TILE_CACHE; i++) {
        if (tiles[i].px && tiles[i].z == z && tiles[i].x == x && tiles[i].y == y) return &tiles[i].dsc;
    }
    int i = tile_next++ % TILE_CACHE;
    if (!tiles[i].px) tiles[i].px = malloc(TILE * TILE * 2);
    tiles[i].z = z, tiles[i].x = x, tiles[i].y = y;
    tile_paint(tiles[i].px, z, x, y);
    tiles[i].dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = TILE, .h = TILE, .stride = TILE * 2},
        .data_size = TILE * TILE * 2,
        .data = (const uint8_t *)tiles[i].px,
    };
    return &tiles[i].dsc;
}

/* --- camera and assistant --- */

void svc_camera_link(bool on)
{
    S.cam.status = on ? CAM_READY : CAM_OFF;
    snprintf(S.cam.model, sizeof(S.cam.model), "DC-S5");
    S.cam.battery = 64, S.cam.shots_left = 812;
    bus_emit(EVT_CAMERA, NULL);
}

void svc_camera(cam_cmd_t cmd)
{
    if (cmd == CAMCMD_RECORD_TOGGLE) S.cam.recording = !S.cam.recording;
    if (cmd == CAMCMD_SHUTTER && S.cam.shots_left > 0) S.cam.shots_left--;
    bus_emit(EVT_CAMERA, NULL);
}

void svc_agent_listen(bool on)
{
    S.agent.status = on ? AGENT_LISTENING : AGENT_THINKING;
    bus_emit(EVT_AGENT, NULL);
}

void svc_agent_cancel(void)
{
    S.agent.status = AGENT_IDLE;
    bus_emit(EVT_AGENT, NULL);
}

/* --- sample data --- */

void sim_services_init(void)
{
    S.batt = (batt_t){.percent = 78, .charging = false, .plugged = false};
    S.link = (link_t){.status = LINK_CONNECTED, .peer = PEER_IPHONE};
    snprintf(S.link.name, sizeof(S.link.name), "iPhone");
    S.wifi = (wifi_t){.connected = true};
    snprintf(S.wifi.ssid, sizeof(S.wifi.ssid), "Home");
    snprintf(S.wifi.ip, sizeof(S.wifi.ip), "192.168.1.42");
    S.steps = 3482;
    S.weather = (weather_t){.valid = true, .temp_c = 18, .hi_c = 21, .lo_c = 12};
    snprintf(S.weather.text, sizeof(S.weather.text), "Clear");

    S.media = (media_t){.valid = true, .playing = true, .position_s = 74, .duration_s = 221, .volume = 62};
    snprintf(S.media.title, sizeof(S.media.title), "Low Tide");
    snprintf(S.media.artist, sizeof(S.media.artist), "Paper Lanterns");
    snprintf(S.media.album, sizeof(S.media.album), "Harbour");

    S.gps = (gps_t){.fix = true, .lat = -33.8568, .lon = 151.2153, .speed_kmh = 4.2f, .course = 40};
}
