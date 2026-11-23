/* Orbit core: shared types, device state and the event bus.
 *
 * Everything in core/, ui/ and apps/ is portable C on top of LVGL. It runs
 * unchanged on the watch and in the desktop simulator (sim/), and it only
 * ever runs on the LVGL thread. Hardware lives behind core/svc.h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "lvgl.h"

#define ORBIT_VERSION "0.1.0"
#define SCR 466 /* round 466 x 466 panel */
#define SCR_C (SCR / 2)

/* ------------------------------------------------------------------ events */

typedef enum {
    EVT_SECOND,   /* once a second while the display is active */
    EVT_MINUTE,   /* minute rolled over (also fires in always-on mode) */
    EVT_BATTERY,
    EVT_LINK,     /* Bluetooth link changed */
    EVT_WIFI,
    EVT_MEDIA,
    EVT_NOTIF,    /* data: notif_t* when one just arrived, else NULL */
    EVT_CALL,
    EVT_GPS,
    EVT_WEATHER,
    EVT_CAMERA,
    EVT_AGENT,
    EVT_PHOTOS,   /* photo library changed */
    EVT_TILE,     /* a map tile finished loading */
    EVT_SETTINGS,
    EVT_DISPLAY,  /* display power state changed */
    EVT_MACROS,
    EVT_TUNER,    /* a new pitch reading from the microphone */
    EVT_AGENDA,   /* the calendar entries from the phone changed */
    EVT_GAME,     /* the Vision or ZZZ app's choices changed */
} evt_t;

typedef void (*bus_cb_t)(evt_t e, void *data, void *user);

void bus_emit(evt_t e, void *data);
/* Subscribes until `owner` is deleted, so screens never leak subscriptions. */
void bus_watch(lv_obj_t *owner, bus_cb_t cb, void *user);

/* ------------------------------------------------------------------- state */

typedef struct {
    int percent; /* -1 when unknown */
    bool charging;
    bool plugged;
} batt_t;

typedef enum { LINK_OFF, LINK_WAITING, LINK_CONNECTED } link_status_t;
typedef enum { PEER_NONE, PEER_IPHONE, PEER_ANDROID, PEER_KEYS } peer_kind_t;

typedef struct {
    link_status_t status;
    peer_kind_t peer;
    bool keys_mode; /* acting as a Bluetooth keyboard instead of a phone companion */
    bool midi_mode; /* acting as a Bluetooth MIDI instrument */
    bool app;       /* the Orbit phone app is on the other end (richer than Gadgetbridge) */
    char name[32];
} link_t;

typedef struct {
    bool connected;
    bool setup_mode; /* hosting its own hotspot for first-time setup */
    char ssid[33];
    char ip[16];
    char setup_pass[12]; /* password of the setup hotspot, new each time */
    bool joining;        /* trying a network chosen on the watch */
    bool failed;         /* that attempt did not work (wrong password, out of range) */
} wifi_t;

#define WIFI_SCAN_MAX 12
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_net_t;

typedef struct {
    bool valid;
    bool playing;
    char title[64];
    char artist[48];
    char album[48];
    int position_s; /* position when `stamp_ms` was taken */
    int duration_s; /* 0 when unknown */
    int volume;     /* 0..100, -1 when unknown */
    uint32_t stamp_ms;
    /* Extras the Orbit phone app reports. Gadgetbridge and iPhones leave them clear. */
    char app[20];   /* which player, e.g. "Spotify" */
    bool shuffle, repeat;
    bool can_seek, can_shuffle, can_repeat, can_like;
    bool has_art;   /* svc_album_art() has a picture for this track */
} media_t;

#define NOTIF_MAX 16
typedef struct {
    uint32_t id;
    char app[24];
    char title[48];
    char body[160];
    time_t when;
    bool no_reply; /* the phone says this one cannot take a typed reply */
} notif_t;

typedef struct {
    bool active;
    char who[40];
} call_t;

typedef struct {
    bool fix;
    double lat, lon;
    float speed_kmh;
    float course;
    uint32_t stamp_ms;
} gps_t;

typedef struct {
    bool valid;
    int temp_c, hi_c, lo_c;
    char text[24];
} weather_t;

typedef enum { CAM_OFF, CAM_CONNECTING, CAM_READY, CAM_ERROR } cam_status_t;
typedef struct {
    cam_status_t status;
    bool recording;
    int battery; /* 0..100, -1 unknown */
    int shots_left;
    char model[24];
    char note[48];
} cam_t;

typedef enum { AGENT_IDLE, AGENT_LISTENING, AGENT_THINKING, AGENT_SPEAKING, AGENT_ERROR } agent_status_t;
typedef struct {
    agent_status_t status;
    float level; /* live audio level 0..1, drives the animation */
    char heard[160];
    char reply[480];
    char error[80];
} agent_t;

/* One calendar entry, sent by the Orbit phone app. */
#define AGENDA_MAX 64   /* how many upcoming entries the watch holds, and */
#define AGENDA_DAYS 30  /* how far ahead they reach: the phone is told both */
#define DAY_MAX 12      /* entries held for the one day open in Calendar */
#define BUSY_MONTHS 3   /* months whose busy days are remembered */
typedef struct {
    time_t start, end; /* UTC seconds */
    bool all_day;
    char title[64];
    char place[48];
} agenda_item_t;

/* Quick replies chosen in the phone app. */
#define REPLY_MAX 5
#define REPLY_LEN 48

typedef enum { DISPLAY_ACTIVE, DISPLAY_AOD, DISPLAY_OFF } display_state_t;

typedef struct {
    batt_t batt;
    link_t link;
    wifi_t wifi;
    media_t media;
    notif_t notifs[NOTIF_MAX]; /* newest first */
    int notif_count;
    call_t call;
    gps_t gps;
    weather_t weather;
    cam_t cam;
    agent_t agent;
    uint32_t steps;
    display_state_t display;
    struct {
        bool valid; /* a steady pitch is being heard */
        float hz;
    } tuner;
    /* Until the power button is pressed: */
    bool theatre;    /* display stays dark */
    bool touch_lock; /* touches are ignored */
    /* Networks found by the last svc_wifi_scan(), strongest first. */
    wifi_net_t scan[WIFI_SCAN_MAX];
    int scan_count;
    bool scanning;
    /* From the Orbit phone app. The calendar and replies outlive the
     * connection; the phone's battery is forgotten when it goes away. */
    agenda_item_t *agenda; /* AGENDA_MAX of them, soonest first; in external RAM */
    int agenda_count;
    bool agenda_known; /* the phone has sent its calendar at least once */
    /* For the Calendar app, both asked of the phone when looked at: which
     * days of a month have something on (bit n is day n; newest month first,
     * year 0 when unused), and the entries of the one day that is open. */
    struct {
        uint16_t year;
        uint8_t month;
        uint32_t days;
    } busy[BUSY_MONTHS];
    struct {
        uint16_t year;
        uint8_t month, day;
        bool known; /* the phone has answered for this day */
        int count;
        agenda_item_t *items; /* DAY_MAX of them */
    } day;
    struct {
        bool known;
        int percent;
        bool charging;
    } phone; /* the phone's own battery */
    char replies[REPLY_MAX][REPLY_LEN];
    int reply_count; /* 0: offer the built-in replies */
} orbit_state_t;

extern orbit_state_t S;

void notif_push(const notif_t *n);
void notif_remove(uint32_t id);
void notif_clear(void);

/* ---------------------------------------------------------------- settings */

enum { FACE_DIGITAL, FACE_ANALOG, FACE_PHOTO, FACE_VISION, FACE_ZZZ, FACE_COUNT };
enum { AOD_OFF, AOD_CLOCK, AOD_PHOTOS, AOD_COUNT };
/* What the two rings on the digital face show. */
enum { COMP_BATTERY, COMP_STEPS, COMP_WEATHER, COMP_DAY, COMP_NONE, COMP_COUNT };
/* Tiles beside the watch face. 0 ends the list. */
enum { TILE_NONE, TILE_MUSIC, TILE_ACTIVITY, TILE_WEATHER, TILE_TIMER, TILE_ALARM, TILE_PET, TILE_AGENDA, TILE_KINDS };
#define TILE_SLOTS 6

typedef struct {
    uint8_t version;
    uint8_t brightness;       /* 5..100 */
    uint8_t face;
    uint8_t aod_mode;
    uint8_t timeout_s;        /* seconds of inactivity before the display rests */
    uint8_t raise_to_wake;
    uint8_t dnd;
    uint8_t clock_24h;
    uint8_t accent;           /* index into the accent palette */
    uint8_t photo_minutes;    /* minutes each photo stays up in always-on mode */
    uint8_t sound;
    uint8_t bedtime;          /* display stays dark and silent until a button is pressed */
    int16_t tz_minutes;       /* local time offset from UTC */
    uint8_t saver;            /* battery saver: no always-on, no raise to wake, dimmer */
    uint8_t comp_left, comp_right;
    uint8_t tiles[TILE_SLOTS]; /* first one sits left of the watch face, the rest to the right */
    uint8_t setup_done;       /* the first-boot setup has been completed */
    uint8_t volume;           /* speaker volume, 0..100 */
    uint8_t previews;         /* new notifications take over the screen briefly */
    uint8_t charge_limit;     /* battery health: cap charging at 80% */
    uint16_t step_goal;
} settings_t;

extern settings_t CFG;

void settings_load(void);
void settings_changed(void); /* persists and tells the UI */

/* ------------------------------------------------------------------ macros */

enum { MACRO_KEYS, MACRO_TEXT, MACRO_MEDIA };
#define MACRO_MAX 14
typedef struct {
    char label[12];
    uint8_t type;
    uint8_t mods;    /* HID modifier bits: 1 ctrl, 2 shift, 4 alt, 8 gui */
    uint8_t key;     /* HID usage id */
    uint16_t usage;  /* consumer usage for MACRO_MEDIA */
    char text[48];
} macro_t;

extern macro_t MACROS[MACRO_MAX];
void macros_load(void);
void macros_changed(void);
void macro_fire(int index);

/* ------------------------------------------------------------------- misc */

void orbit_boot(void); /* load settings + build the UI; call with LVGL ready */
void fmt_clock(char *buf, size_t len, const struct tm *t, bool seconds);
const char *fmt_ampm(const struct tm *t); /* "" in 24 h mode */
