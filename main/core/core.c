#include <stdio.h>
#include <string.h>

#include "orbit.h"
#include "svc.h"

orbit_state_t S = {
    .batt = {.percent = -1},
    .media = {.volume = -1},
    .cam = {.battery = -1, .shots_left = -1},
};

/* --------------------------------------------------------------------- bus */

#define BUS_MAX 40
static struct {
    bus_cb_t cb;
    void *user;
    lv_obj_t *owner;
} subs[BUS_MAX];

static void owner_deleted(lv_event_t *e)
{
    lv_obj_t *owner = lv_event_get_target_obj(e);
    for (int i = 0; i < BUS_MAX; i++) {
        if (subs[i].owner == owner) subs[i].cb = NULL, subs[i].owner = NULL;
    }
}

void bus_watch(lv_obj_t *owner, bus_cb_t cb, void *user)
{
    bool first = true;
    for (int i = 0; i < BUS_MAX; i++) {
        if (subs[i].owner == owner) first = false;
    }
    for (int i = 0; i < BUS_MAX; i++) {
        if (subs[i].cb) continue;
        subs[i].cb = cb, subs[i].user = user, subs[i].owner = owner;
        if (first) lv_obj_add_event_cb(owner, owner_deleted, LV_EVENT_DELETE, NULL);
        return;
    }
    LV_LOG_WARN("bus full");
}

void bus_emit(evt_t e, void *data)
{
    for (int i = 0; i < BUS_MAX; i++) {
        if (subs[i].cb) subs[i].cb(e, data, subs[i].user);
    }
}

/* ----------------------------------------------------------- notifications */

void notif_push(const notif_t *n)
{
    /* A repeat of the same id replaces the old entry. */
    for (int i = 0; i < S.notif_count; i++) {
        if (S.notifs[i].id == n->id) {
            memmove(&S.notifs[i], &S.notifs[i + 1], sizeof(notif_t) * (size_t)(S.notif_count - i - 1));
            S.notif_count--;
            break;
        }
    }
    int keep = S.notif_count < NOTIF_MAX ? S.notif_count : NOTIF_MAX - 1;
    memmove(&S.notifs[1], &S.notifs[0], sizeof(notif_t) * (size_t)keep);
    S.notifs[0] = *n;
    S.notif_count = keep + 1;
    bus_emit(EVT_NOTIF, &S.notifs[0]);
}

void notif_remove(uint32_t id)
{
    for (int i = 0; i < S.notif_count; i++) {
        if (S.notifs[i].id != id) continue;
        memmove(&S.notifs[i], &S.notifs[i + 1], sizeof(notif_t) * (size_t)(S.notif_count - i - 1));
        S.notif_count--;
        bus_emit(EVT_NOTIF, NULL);
        return;
    }
}

void notif_clear(void)
{
    S.notif_count = 0;
    bus_emit(EVT_NOTIF, NULL);
}

/* ---------------------------------------------------------------- settings */

#define CFG_VERSION 6

settings_t CFG;

static void settings_defaults(void)
{
    CFG = (settings_t){
        .version = CFG_VERSION,
        .brightness = 70,
        .face = FACE_DIGITAL,
        .aod_mode = AOD_CLOCK,
        .timeout_s = 12,
        .raise_to_wake = 1,
        .dnd = 0,
        .clock_24h = 0,
        .accent = 0,
        .photo_minutes = 5,
        .sound = 1,
        .tz_minutes = 0,
        .comp_left = COMP_BATTERY,
        .comp_right = COMP_STEPS,
        .tiles = {TILE_MUSIC, TILE_ACTIVITY, TILE_WEATHER, TILE_TIMER},
        .volume = 70,
        .previews = 1,
        .step_goal = 8000,
    };
}

void settings_load(void)
{
    if (!svc_kv_load("cfg", &CFG, sizeof(CFG)) || (CFG.version != CFG_VERSION && CFG.version != 5)) {
        settings_defaults();
    } else if (CFG.version == 5) {
        /* v5 -> v6: custom_face occupies the former padding byte before step_goal. */
        CFG.custom_face = 0;
        CFG.version = CFG_VERSION;
    }
    if (CFG.brightness < 5) CFG.brightness = 5;
    if (CFG.timeout_s < 5) CFG.timeout_s = 5;
    if (CFG.face >= FACE_COUNT) CFG.face = FACE_DIGITAL;
    if (CFG.aod_mode >= AOD_COUNT) CFG.aod_mode = AOD_CLOCK;
    if (CFG.comp_left >= COMP_COUNT) CFG.comp_left = COMP_BATTERY;
    if (CFG.comp_right >= COMP_COUNT) CFG.comp_right = COMP_STEPS;
    if (CFG.volume > 100) CFG.volume = 70;
    if (CFG.step_goal < 1000) CFG.step_goal = 8000;
    for (int i = 0; i < TILE_SLOTS; i++) {
        if (CFG.tiles[i] >= TILE_KINDS) CFG.tiles[i] = TILE_NONE;
    }
    svc_charge_limit(CFG.charge_limit != 0);
}

void settings_changed(void)
{
    svc_kv_save("cfg", &CFG, sizeof(CFG));
    svc_charge_limit(CFG.charge_limit != 0);
    bus_emit(EVT_SETTINGS, NULL);
}

/* ------------------------------------------------------------------ macros */

macro_t MACROS[MACRO_MAX];

/* HID usage ids used by the defaults. */
#define K_C 0x06
#define K_V 0x19
#define K_Z 0x1D
#define K_D 0x07
#define K_L 0x0F
#define K_S 0x16
#define K_TAB 0x2B
#define K_PRTSC 0x46
#define M_CTRL 1
#define M_SHIFT 2
#define M_ALT 4
#define M_GUI 8

static void macros_defaults(void)
{
    static const macro_t d[MACRO_MAX] = {
        {"Mute", MACRO_MEDIA, 0, 0, 0x00E2, ""},
        {"Copy", MACRO_KEYS, M_CTRL, K_C, 0, ""},
        {"Paste", MACRO_KEYS, M_CTRL, K_V, 0, ""},
        {"Undo", MACRO_KEYS, M_CTRL, K_Z, 0, ""},
        {"Snip", MACRO_KEYS, M_GUI | M_SHIFT, K_S, 0, ""},
        {"Desktop", MACRO_KEYS, M_GUI, K_D, 0, ""},
        {"Lock", MACRO_KEYS, M_GUI, K_L, 0, ""},
        {"Play", MACRO_MEDIA, 0, 0, 0x00CD, ""},
        {"Next", MACRO_MEDIA, 0, 0, 0x00B5, ""},
        {"Prev", MACRO_MEDIA, 0, 0, 0x00B6, ""},
        {"Vol +", MACRO_MEDIA, 0, 0, 0x00E9, ""},
        {"Vol -", MACRO_MEDIA, 0, 0, 0x00EA, ""},
        {"Switch", MACRO_KEYS, M_ALT, K_TAB, 0, ""},
        {"Shot", MACRO_KEYS, 0, K_PRTSC, 0, ""},
    };
    memcpy(MACROS, d, sizeof(MACROS));
}

void macros_load(void)
{
    if (!svc_kv_load("macros", MACROS, sizeof(MACROS))) macros_defaults();
    for (int i = 0; i < MACRO_MAX; i++) {
        MACROS[i].label[sizeof(MACROS[i].label) - 1] = 0;
        MACROS[i].text[sizeof(MACROS[i].text) - 1] = 0;
    }
}

void macros_changed(void)
{
    svc_kv_save("macros", MACROS, sizeof(MACROS));
    bus_emit(EVT_MACROS, NULL);
}

void macro_fire(int index)
{
    if (index < 0 || index >= MACRO_MAX) return;
    const macro_t *m = &MACROS[index];
    switch (m->type) {
    case MACRO_KEYS: svc_hid_keys(m->mods, m->key); break;
    case MACRO_TEXT: svc_hid_text(m->text); break;
    case MACRO_MEDIA: svc_hid_consumer(m->usage); break;
    }
}

/* ----------------------------------------------------------- custom faces */

typedef struct {
    uint8_t count;
    custom_face_t items[CUSTOM_FACE_MAX];
} custom_faces_blob_t;

custom_face_t CUSTOM_FACES[CUSTOM_FACE_MAX];
int CUSTOM_FACE_COUNT;
uint32_t CUSTOM_FACES_GEN;

static void custom_faces_defaults(void)
{
    CUSTOM_FACE_COUNT = 2;
    memset(CUSTOM_FACES, 0, sizeof(CUSTOM_FACES));
    CUSTOM_FACES[0] = (custom_face_t){
        .name = "Midnight", .style = CF_STYLE_MINIMAL, .accent = 0,
        .photo_idx = 0, .show_date = 1, .show_comps = 0, .bg_hex = 0x071224,
    };
    CUSTOM_FACES[1] = (custom_face_t){
        .name = "Ember", .style = CF_STYLE_COMPACT, .accent = 4,
        .photo_idx = 0, .show_date = 1, .show_comps = 1, .bg_hex = 0x1A0B0A,
    };
}

void custom_faces_load(void)
{
    custom_faces_blob_t b;
    if (!svc_kv_load("cfaces", &b, sizeof(b)) || b.count == 0 || b.count > CUSTOM_FACE_MAX) {
        custom_faces_defaults();
        return;
    }
    CUSTOM_FACE_COUNT = b.count;
    memcpy(CUSTOM_FACES, b.items, sizeof(CUSTOM_FACES));
    for (int i = 0; i < CUSTOM_FACE_COUNT; i++) {
        CUSTOM_FACES[i].name[sizeof(CUSTOM_FACES[i].name) - 1] = 0;
        if (CUSTOM_FACES[i].style >= CF_STYLE_COUNT) CUSTOM_FACES[i].style = CF_STYLE_DIGITAL;
        if (CUSTOM_FACES[i].accent >= 6) CUSTOM_FACES[i].accent = 0;
    }
    if (CFG.custom_face >= CUSTOM_FACE_COUNT) CFG.custom_face = 0;
}

void custom_faces_changed(void)
{
    if (CUSTOM_FACE_COUNT < 1) custom_faces_defaults();
    if (CUSTOM_FACE_COUNT > CUSTOM_FACE_MAX) CUSTOM_FACE_COUNT = CUSTOM_FACE_MAX;
    if (CFG.custom_face >= CUSTOM_FACE_COUNT) CFG.custom_face = 0;
    custom_faces_blob_t b = {.count = (uint8_t)CUSTOM_FACE_COUNT};
    memcpy(b.items, CUSTOM_FACES, sizeof(b.items));
    svc_kv_save("cfaces", &b, sizeof(b));
    CUSTOM_FACES_GEN++;
    bus_emit(EVT_SETTINGS, NULL);
}

/* -------------------------------------------------------------- formatting */

void fmt_clock(char *buf, size_t len, const struct tm *t, bool seconds)
{
    int h = t->tm_hour;
    if (!CFG.clock_24h) {
        h %= 12;
        if (h == 0) h = 12;
    }
    if (seconds) snprintf(buf, len, CFG.clock_24h ? "%02d:%02d:%02d" : "%d:%02d:%02d", h, t->tm_min, t->tm_sec);
    else snprintf(buf, len, CFG.clock_24h ? "%02d:%02d" : "%d:%02d", h, t->tm_min);
}

const char *fmt_ampm(const struct tm *t)
{
    if (CFG.clock_24h) return "";
    return t->tm_hour < 12 ? "AM" : "PM";
}
