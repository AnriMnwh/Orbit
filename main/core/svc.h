/* Services the portable UI needs from the hardware.
 *
 * Implemented twice: main/platform/ (the watch) and sim/ (the desktop
 * simulator). All of these are called from the LVGL thread and must return
 * quickly; anything slow is queued by the implementation, which reports back
 * by updating `S` and emitting a bus event on the LVGL thread.
 */
#pragma once

#include "orbit.h"

/* --- time --- */
uint32_t svc_millis(void);
time_t svc_time(void);                 /* UTC seconds */
void svc_localtime(struct tm *out);    /* honours CFG.tz_minutes */
bool svc_time_valid(void);             /* false until the clock has been set */

/* --- small persistent blobs --- */
bool svc_kv_load(const char *key, void *buf, size_t len);
void svc_kv_save(const char *key, const void *buf, size_t len);

/* --- display, sound, power --- */
void svc_brightness(int percent);      /* 0 turns the panel dark */
void svc_display_active(bool active);  /* hint for adaptive polling & light sleep */
enum { SND_TAP, SND_NOTIFY, SND_ALARM, SND_OK, SND_ERROR };
void svc_sound(int id);
void svc_power_off(void);
void svc_restart(void);
void svc_charge_limit(bool on);        /* cap battery charging at 80% for battery health */
void svc_storage_info(uint32_t *total_kb, uint32_t *free_kb);
/* Free working memory: the chip's own RAM and the 8 MB external RAM. */
void svc_memory_info(uint32_t *internal_kb, uint32_t *external_kb);

/* --- Bluetooth link --- */
typedef enum {
    MEDIA_TOGGLE, MEDIA_NEXT, MEDIA_PREV, MEDIA_VOL_UP, MEDIA_VOL_DOWN,
    /* Only with the Orbit phone app, and only when S.media.can_* says so: */
    MEDIA_SHUFFLE, MEDIA_REPEAT, MEDIA_LIKE,
} media_cmd_t;
void svc_media_seek(int seconds);          /* jump to a position in the track */
/* The current track's picture, full-screen and already dimmed to sit behind
 * text, or NULL. Valid until the next EVT_MEDIA. */
const lv_image_dsc_t *svc_album_art(void);
void svc_link_keys_mode(bool on);
void svc_link_forget(void);            /* drop all pairings */
void svc_pair_answer(bool accept);    /* the owner's answer to shell_pairing() */
void svc_media(media_cmd_t cmd);
void svc_hid_keys(uint8_t mods, uint8_t key);
void svc_hid_text(const char *text);
void svc_hid_consumer(uint16_t usage);
void svc_find_phone(bool on);
void svc_gps_want(bool on);            /* ask the phone to stream its location */
void svc_notif_dismiss(uint32_t id);
void svc_call(bool accept);

void svc_notif_reply(uint32_t id, const char *text); /* Android only */

void svc_set_time(time_t utc);           /* set the clock by hand */
/* Wipe settings, pairings, saved networks and keys, photos and maps, then restart. */
void svc_factory_reset(void);
enum { CLEAR_PHOTOS, CLEAR_MAPS };
void svc_clear(int what);
enum { HAS_WIFI, HAS_AI_KEY, HAS_CAMERA };
bool svc_configured(int what);           /* has this been saved on the setup page? */

/* --- Wi-Fi --- */
void svc_wifi_enable(bool on);
void svc_wifi_setup_mode(bool on);
void svc_web_enable(bool on);
void svc_wifi_scan(void);                /* fills S.scan, then EVT_WIFI */
void svc_wifi_join(const char *ssid, const char *password);
void svc_wifi_forget(void);
/* Accept a firmware upload from the setup page for the next few minutes.
 * Returns how many seconds the window stays open. */
int svc_update_allow(void);

/* --- photos (always 466 x 466, decoded on demand) --- */
int svc_photo_count(void);
/* Returns a decoded image that stays valid until the next call, or NULL. */
const lv_image_dsc_t *svc_photo(int index);

/* --- map tiles (256 x 256). NULL while missing; EVT_TILE fires on arrival --- */
const lv_image_dsc_t *svc_tile(int z, int x, int y);

/* --- camera --- */
typedef enum { CAMCMD_SHUTTER, CAMCMD_RECORD_TOGGLE, CAMCMD_FOCUS } cam_cmd_t;
void svc_camera_link(bool on);
void svc_camera(cam_cmd_t cmd);

/* --- calendar: asks the Orbit phone app; the answer lands in S, then EVT_AGENDA.
 * Both return false when there is nobody to ask. --- */
bool svc_calendar_month(int year, int month);        /* which days are busy, into S.busy */
bool svc_calendar_day(int year, int month, int day); /* that day's entries, into S.day */

/* --- assistant --- */
void svc_agent_listen(bool on);        /* true: start recording, false: send it */
void svc_agent_cancel(void);
