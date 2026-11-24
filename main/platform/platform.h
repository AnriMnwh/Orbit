/* Shared between the watch-side modules in platform/. Not used by the UI. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "../core/orbit.h"
#include "../core/svc.h"
#include "../ui/ui.h"

#define DATA_DIR "/data"
#define PHOTO_DIR DATA_DIR "/photos"
#define TILE_DIR DATA_DIR "/tiles"

/* Runs fn(arg) on the LVGL thread. Every other task must go through this
 * before touching `S`, `CFG` or any LVGL object. */
void ui_post(void (*fn)(void *arg), void *arg);

/* --- board.c --- */
void clock_set(time_t utc, int tz_minutes, bool tz_known);

/* --- storage.c --- */
void storage_init(void);
#define PHOTO_LIMIT 60     /* photos the library holds */
void photos_changed(void); /* call from any task after adding/removing photos */
int photos_list(char names[][24], int max);
bool photos_new_path(char *path, size_t len, char name[24]); /* false when full */
/* Stores a 466 x 466 JPEG; NULL, or a one-word reason. Writes flash. */
const char *photo_add(const uint8_t *jpg, size_t len, char name[24]);
bool photo_remove(const char *name);

/* --- audio.c --- */
void audio_init(void);
/* Plays signed 16-bit mono samples; returns early if audio_stop() is called. */
void audio_play(const int16_t *pcm, size_t samples, int rate);
void audio_stop(void);
bool audio_record_start(void);
/* Stops and hands over the recording (16 kHz mono). Caller frees with free(). */
int16_t *audio_record_stop(size_t *samples);
float audio_level(void); /* 0..1, current microphone or speaker loudness */

/* --- net.c --- */
enum {
    NET_NEED_SETUP = 1, NET_NEED_MANUAL = 2, NET_NEED_CAMERA = 4,
    NET_NEED_AGENT = 8, NET_NEED_TILES = 16, NET_NEED_NTP = 32,
};
void net_init(void);
void net_acquire(uint8_t reason);
void net_release(uint8_t reason);
bool net_online(void);
/* Leaves the home network for the camera's own Wi-Fi and back again. */
bool net_join_camera(const char *ssid, const char *password);
void net_leave_camera(void);
bool net_secret_get(const char *key, char *out, size_t len);
void net_secret_set(const char *key, const char *value);
void net_rejoin(void); /* after the saved home network changed */

/* --- web.c --- */
void web_enable(bool on);
const char *web_pin_get(void);
bool web_is_active(void);
bool web_update_window_open(void);

/* --- ble*.c --- */
void ble_init(void);
bool ble_phone_is_android(void);
void ble_mark_iphone(void);
/* Gadgetbridge link (Android): send one JSON line to the phone. */
void gb_send(const char *json);
void gb_send_wait(const char *json); /* waits for room; never from the Bluetooth or screen threads */
void gb_receive(const uint8_t *data, size_t len);
void gb_connected(bool up);
/* Apple services (iPhone), discovered on the connection the phone opens. */
void apple_start(uint16_t conn);
void apple_stop(void);
bool apple_active(void);
void apple_media(media_cmd_t cmd);
void apple_notif_action(uint32_t uid, bool positive);
int apple_on_notify(uint16_t conn, uint16_t attr_handle, const uint8_t *data, size_t len);
/* Paced sending for the phone link: lines and bulk frames queue up and go out
 * as fast as the radio accepts them. `wait` blocks the caller while the queue
 * is full (never use it from a Bluetooth callback). */
void ble_tx_bulk(const uint8_t *frame, size_t len, bool wait);
int ble_payload(void);         /* largest frame the link carries right now */
void ble_request_pairing(void);
void ble_set_app(bool on);     /* the Orbit phone app said hello */

/* --- link.c: what the Orbit phone app adds on top of Gadgetbridge --- */
struct cJSON;
bool link_handle(const char *type, struct cJSON *msg); /* true if it was one of ours */
void link_bulk_rx(const uint8_t *frame, size_t len);
void link_art_key(const char *key); /* the track changed to one with this picture ("" = none) */
void link_reset(void);         /* the phone went away */
void link_encrypted(void);     /* pairing finished and the link is now private */
bool ble_encrypted(void);
bool link_app(void);
bool link_can_agent(void);     /* the app offered to run the assistant */
/* The assistant through the phone: sends the recorded question, then waits
 * for the answer. Returns false if the phone path failed before it started. */
bool link_agent_ask(const int16_t *pcm, size_t samples, volatile bool *cancelled);
void link_agent_cancel(void);
bool jpeg_to_rgb565(const uint8_t *jpg, size_t len, int w, int h, uint16_t *out);
/* What the app's hello offered that the watch only uses when asked to. */
enum {
    LINK_CAP_AGENT = 1, LINK_CAP_CFG = 2, LINK_CAP_ACT = 4, LINK_CAP_BUSY = 8, LINK_CAP_DAY = 16,
};
bool link_has(int cap);
uint32_t link_crc32(const uint8_t *p, size_t n); /* the zlib one */
/* Answers a "blob" line. `extra` is more JSON fields, each starting with a
 * comma, or "". `wait` as for gb_send_wait(). */
void link_blob_reply(const char *kind, const char *key, bool ok, const char *extra, bool wait);

/* --- link_more.c: the app's optional extras (place, agenda, photos, cfg,
 * act, replies, phone) --- */
bool more_handle(const char *type, struct cJSON *msg); /* on Bluetooth's task */
void more_hello(void);  /* the app has just been answered (Bluetooth's task) */
void more_gone(void);   /* the app went away (LVGL thread) */
/* A whole photo has arrived. Takes ownership of `jpg`. */
void more_photo(uint8_t *jpg, size_t len, uint32_t crc, const char *key);
bool more_photo_busy(void);
void more_ota_notify(void);

/* --- agent.c, used by link.c --- */
const char *agent_tools_json(void);
void agent_run_tool(const char *name, struct cJSON *args, char *out, size_t len);
void agent_publish(agent_status_t status, const char *heard, const char *reply, const char *error);
void agent_play(const int16_t *pcm, size_t samples, int rate);

void hid_keys(uint8_t mods, uint8_t key);
void hid_consumer(uint16_t usage);
void hid_text(const char *text);

/* --- features --- */
void tiles_init(void);
/* Download the maps around a point for offline use. Returns tiles queued. */
int tiles_preload(double lat, double lon);
void tiles_progress(int *done, int *total);
void lumix_init(void);
void agent_init(void);
