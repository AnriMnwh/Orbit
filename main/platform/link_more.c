/* The Orbit phone app's optional extras ("More the app can already do" in
 * companion/PROTOCOL.md). Each is offered by name in the watch's hello:
 *
 *   place    roughly where the phone is, for Sun Times
 *   agenda   the upcoming calendar entries, a month ahead
 *   busy     which days of a month have something on, for Calendar
 *   day      one day's entries, asked for when it is opened in Calendar
 *   photos   pictures for the library, over the bulk channel
 *   cfg      the watch's settings, read and changed from the phone
 *   act      today's step count, for the phone to keep a history
 *   replies  the quick replies offered under a notification
 *   phone    the phone's own battery
 *
 * Lines arrive on Bluetooth's task. Anything that touches `S`, `CFG` or the
 * saved settings is handed to the LVGL thread; a photo is written to flash by
 * a short-lived task of its own.
 *
 * Written against the protocol document; never run against the app.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "platform.h"

static const char *TAG = "link+";

static const char *str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static double num(cJSON *j, const char *key, double fallback)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

/* Copies text without cutting a UTF-8 character in half. */
static void text_copy(char *out, size_t size, const char *in)
{
    size_t n = strlen(in);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((uint8_t)in[n] & 0xC0) == 0x80) n--;
    }
    memcpy(out, in, n);
    out[n] = 0;
}

/* ------------------------------------------------------------------ place */

static void place_apply(void *arg)
{
    double *now = arg, old[2];
    /* Sent hourly and usually unchanged: only a move is worth a flash write. */
    if (!svc_kv_load("place", old, sizeof(old)) || old[0] != now[0] || old[1] != now[1]) {
        svc_kv_save("place", now, 2 * sizeof(double));
    }
    free(now);
}

static void place_message(cJSON *msg)
{
    double lat = num(msg, "lat", NAN), lon = num(msg, "lon", NAN);
    if (!(lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180)) return;
    double *p = malloc(2 * sizeof(double));
    if (!p) return;
    p[0] = lat, p[1] = lon;
    ui_post(place_apply, p);
}

/* --------------------------------------------- agenda, busy days, one day */

static void item_read(cJSON *msg, agenda_item_t *it)
{
    it->start = (time_t)num(msg, "s", 0), it->end = (time_t)num(msg, "e", 0);
    it->all_day = cJSON_IsTrue(cJSON_GetObjectItem(msg, "allday"));
    text_copy(it->title, sizeof(it->title), str(msg, "title"));
    text_copy(it->place, sizeof(it->place), str(msg, "loc"));
}

typedef struct {
    int i, n;
    agenda_item_t item;
} agenda_msg_t;

static void agenda_apply(void *arg)
{
    agenda_msg_t *m = arg;
    if (S.agenda) {
        if (m->n <= 0) {
            S.agenda_count = 0; /* nothing coming up */
        } else {
            if (m->i == 0) S.agenda_count = 0; /* a new list starts */
            if (m->i == S.agenda_count && m->i < AGENDA_MAX) S.agenda[S.agenda_count++] = m->item;
        }
        S.agenda_known = true;
        /* A long list arrives a line at a time over a few seconds: the
         * screens are told once, when the last line is in. */
        if (m->n <= 0 || m->i >= m->n - 1 || S.agenda_count >= AGENDA_MAX) bus_emit(EVT_AGENDA, NULL);
    }
    free(m);
}

static void agenda_message(cJSON *msg)
{
    agenda_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->i = (int)num(msg, "i", 0), m->n = (int)num(msg, "n", 0);
    item_read(msg, &m->item);
    ui_post(agenda_apply, m);
}

typedef struct {
    int year, month;
    uint32_t days;
} busy_msg_t;

static void busy_apply(void *arg)
{
    busy_msg_t *m = arg;
    int at = BUSY_MONTHS - 1; /* the oldest goes, unless this month is already held */
    for (int i = 0; i < BUSY_MONTHS; i++) {
        if (S.busy[i].year == m->year && S.busy[i].month == m->month) at = i;
    }
    memmove(&S.busy[1], &S.busy[0], sizeof(S.busy[0]) * (size_t)at);
    S.busy[0].year = (uint16_t)m->year, S.busy[0].month = (uint8_t)m->month, S.busy[0].days = m->days;
    free(m);
    bus_emit(EVT_AGENDA, NULL);
}

static void busy_message(cJSON *msg)
{
    int year = 0, month = 0;
    cJSON *days = cJSON_GetObjectItem(msg, "days"), *v;
    if (!cJSON_IsArray(days) || sscanf(str(msg, "month"), "%d-%d", &year, &month) != 2) return;
    if (year < 2000 || year > 2199 || month < 1 || month > 12) return;
    busy_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->year = year, m->month = month;
    cJSON_ArrayForEach(v, days) {
        if (cJSON_IsNumber(v) && v->valueint >= 1 && v->valueint <= 31) m->days |= 1u << v->valueint;
    }
    ui_post(busy_apply, m);
}

typedef struct {
    int year, month, day, i, n;
    agenda_item_t item;
} day_msg_t;

static void day_apply(void *arg)
{
    day_msg_t *m = arg;
    /* Only the day that is open is wanted; an answer for one the user has
     * already left is dropped. */
    if (S.day.items && m->year == S.day.year && m->month == S.day.month && m->day == S.day.day) {
        if (m->n <= 0) {
            S.day.count = 0, S.day.known = true;
        } else {
            if (m->i == 0) S.day.count = 0, S.day.known = false;
            if (m->i == S.day.count && m->i < DAY_MAX) S.day.items[S.day.count++] = m->item;
            if (m->i >= m->n - 1 || S.day.count >= DAY_MAX) S.day.known = true;
        }
        if (S.day.known) bus_emit(EVT_AGENDA, NULL);
    }
    free(m);
}

static void day_message(cJSON *msg)
{
    day_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    if (sscanf(str(msg, "date"), "%d-%d-%d", &m->year, &m->month, &m->day) != 3) {
        free(m);
        return;
    }
    m->i = (int)num(msg, "i", 0), m->n = (int)num(msg, "n", 0);
    item_read(msg, &m->item);
    ui_post(day_apply, m);
}

/* Called by the Calendar app, on the LVGL thread. */
bool svc_calendar_month(int year, int month)
{
    if (!link_has(LINK_CAP_BUSY)) return false;
    char line[64];
    snprintf(line, sizeof(line), "{\"t\":\"busy\",\"month\":\"%04d-%02d\"}", year % 10000, month % 100);
    gb_send(line);
    return true;
}

bool svc_calendar_day(int year, int month, int day)
{
    if (!link_has(LINK_CAP_DAY)) return false;
    char line[64];
    snprintf(line, sizeof(line), "{\"t\":\"day\",\"date\":\"%04d-%02d-%02d\"}", year % 10000, month % 100, day % 100);
    gb_send(line);
    return true;
}

/* ---------------------------------------------------------------- replies */

typedef struct {
    int count;
    char list[REPLY_MAX][REPLY_LEN];
} replies_msg_t;

static void replies_apply(void *arg)
{
    replies_msg_t *m = arg;
    memcpy(S.replies, m->list, sizeof(S.replies));
    S.reply_count = m->count; /* none: back to the built-in ones */
    free(m);
}

static void replies_message(cJSON *msg)
{
    replies_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItem(msg, "list")) {
        if (m->count >= REPLY_MAX) break;
        if (cJSON_IsString(v) && v->valuestring[0]) text_copy(m->list[m->count++], REPLY_LEN, v->valuestring);
    }
    ui_post(replies_apply, m);
}

/* ---------------------------------------------------------- phone battery */

typedef struct {
    int percent;
    bool charging;
} phone_msg_t;

static void phone_apply(void *arg)
{
    phone_msg_t *m = arg;
    S.phone.known = true, S.phone.percent = m->percent, S.phone.charging = m->charging;
    free(m);
    bus_emit(EVT_LINK, NULL);
}

static void phone_message(cJSON *msg)
{
    double bat = num(msg, "bat", NAN);
    if (!(bat >= 0 && bat <= 100)) return;
    phone_msg_t *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->percent = (int)lround(bat), m->charging = cJSON_IsTrue(cJSON_GetObjectItem(msg, "chg"));
    ui_post(phone_apply, m);
}

/* ------------------------------------------------- settings and step count
 * Everything in this block runs on the LVGL thread. */

static bool watching, act_due;
static char cfg_sent[384]; /* the last settings line the phone was given */

static void cfg_send(void)
{
    if (!link_has(LINK_CAP_CFG)) return;
    char faces[32] = "[", styles[32] = "[";
    int fn = 0;
    for (int i = 0; i < FACE_COUNT; i++) {
        if (CFG.face_order[i] < FACE_COUNT) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%s%d", fn++ ? "," : "", CFG.face_order[i]);
            strlcat(faces, buf, sizeof(faces));
        }
        char sbuf[8];
        snprintf(sbuf, sizeof(sbuf), "%s%d", i ? "," : "", CFG.face_style[i]);
        strlcat(styles, sbuf, sizeof(styles));
    }
    strlcat(faces, "]", sizeof(faces));
    strlcat(styles, "]", sizeof(styles));

    char line[384];
    snprintf(line, sizeof(line),
             "{\"t\":\"cfg\",\"brightness\":%d,\"dnd\":%s,\"h24\":%s,\"raise\":%s,\"sound\":%s,\"aod\":%d,\"timeout\":%d,"
             "\"goal\":%d,\"face\":%d,\"accent\":%d,\"compL\":%d,\"compR\":%d,\"grid\":%s,\"faces\":%s,\"styles\":%s}",
             CFG.brightness, CFG.dnd ? "true" : "false", CFG.clock_24h ? "true" : "false", CFG.raise_to_wake ? "true" : "false",
             CFG.sound ? "true" : "false", CFG.aod_mode, CFG.timeout_s, CFG.step_goal, CFG.face, CFG.accent, CFG.comp_left,
             CFG.comp_right, CFG.grid_launcher ? "true" : "false", faces, styles);
    if (strcmp(line, cfg_sent) == 0) return; /* some other setting changed */
    strlcpy(cfg_sent, line, sizeof(cfg_sent));
    gb_send(line);
}

static void act_send(void)
{
    if (!link_has(LINK_CAP_ACT)) {
        act_due = false;
        return;
    }
    if (!svc_time_valid()) return; /* no date yet: stays due and is tried again each minute */
    struct tm now;
    svc_localtime(&now);
    char line[80];
    snprintf(line, sizeof(line), "{\"t\":\"act\",\"steps\":%u,\"day\":\"%04d-%02d-%02d\"}", (unsigned)S.steps, now.tm_year + 1900,
             now.tm_mon + 1, now.tm_mday);
    gb_send(line);
    act_due = false;
}

void more_ota_notify(void)
{
    if (!link_app() || !ble_encrypted() || !web_update_window_open()) return;
    const char *ip = S.wifi.setup_mode ? "192.168.4.1" : (S.wifi.connected ? S.wifi.ip : "");
    char line[96];
    snprintf(line, sizeof(line), "{\"t\":\"ota\",\"ip\":\"%s\",\"pin\":\"%s\",\"window\":300}", ip, web_pin_get());
    gb_send(line);
}

static void on_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_SETTINGS) {
        cfg_send();
    } else if (e == EVT_WIFI) {
        if (web_update_window_open()) more_ota_notify();
    } else if (e == EVT_MINUTE) {
        /* Steps go out on the hour, and once more just before the count
         * starts again at midnight so the phone has the day's total. */
        struct tm now;
        svc_localtime(&now);
        if (act_due || now.tm_min == 0 || (now.tm_hour == 23 && now.tm_min == 59)) act_send();
        /* The brightness slider in Settings saves without an event, so the
         * phone hears of that here. Nothing is sent when nothing changed. */
        cfg_send();
    }
}

/* The app has just been answered: tell it where things stand. */
static void start(void *arg)
{
    LV_UNUSED(arg);
    if (!watching) {
        bus_watch(lv_layer_top(), on_event, NULL);
        watching = true;
    }
    cfg_sent[0] = 0;
    cfg_send();
    act_due = true;
    act_send();
}

typedef struct {
    char key[12];
    int value;
    uint8_t arr[FACE_COUNT];
    int arr_len;
} cfg_msg_t;

static int within(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void cfg_apply(void *arg)
{
    cfg_msg_t *m = arg;
    const char *k = m->key;
    int v = m->value;
    bool known = true;
    if (strcmp(k, "brightness") == 0) CFG.brightness = (uint8_t)within(v, 5, 100);
    else if (strcmp(k, "dnd") == 0) CFG.dnd = v != 0;
    else if (strcmp(k, "h24") == 0) CFG.clock_24h = v != 0;
    else if (strcmp(k, "raise") == 0) CFG.raise_to_wake = v != 0;
    else if (strcmp(k, "sound") == 0) CFG.sound = v != 0;
    else if (strcmp(k, "aod") == 0) CFG.aod_mode = (uint8_t)within(v, 0, AOD_COUNT - 1);
    else if (strcmp(k, "timeout") == 0) CFG.timeout_s = (uint8_t)within(v, 5, 120);
    else if (strcmp(k, "goal") == 0) CFG.step_goal = (uint16_t)within(v, 1000, 60000);
    else if (strcmp(k, "face") == 0) CFG.face = (uint8_t)within(v, 0, FACE_COUNT - 1);
    else if (strcmp(k, "accent") == 0) CFG.accent = (uint8_t)within(v, 0, ACCENT_COUNT - 1);
    else if (strcmp(k, "compL") == 0) CFG.comp_left = (uint8_t)within(v, 0, COMP_COUNT - 1);
    else if (strcmp(k, "compR") == 0) CFG.comp_right = (uint8_t)within(v, 0, COMP_COUNT - 1);
    else if (strcmp(k, "grid") == 0) CFG.grid_launcher = v != 0;
    else if (strcmp(k, "faces") == 0) {
        uint8_t seen = 0, packed[FACE_COUNT];
        int count = 0;
        for (int i = 0; i < m->arr_len && i < FACE_COUNT; i++) {
            uint8_t f = m->arr[i];
            if (f < FACE_COUNT && !(seen & (1u << f))) {
                seen |= (uint8_t)(1u << f);
                packed[count++] = f;
            }
        }
        if (count > 0) {
            for (int i = 0; i < FACE_COUNT; i++) CFG.face_order[i] = i < count ? packed[i] : 0xFF;
            bool vis = false;
            for (int i = 0; i < count; i++) {
                if (CFG.face_order[i] == CFG.face) vis = true;
            }
            if (!vis) CFG.face = CFG.face_order[0];
        } else {
            known = false;
        }
    } else if (strcmp(k, "styles") == 0) {
        for (int i = 0; i < m->arr_len && i < FACE_COUNT; i++) CFG.face_style[i] = (uint8_t)within(m->arr[i], 0, 2);
    } else known = false;
    free(m);
    /* The phone already shows what it asked for, so it is always told what
     * the watch ended up with, even when that is no change at all. */
    cfg_sent[0] = 0;
    if (known) settings_changed(); /* saves, repaints, and comes back as EVT_SETTINGS */
    cfg_send();
}

/* On Bluetooth's task. */
static void cfg_message(cJSON *msg)
{
    cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItem(msg, "set")) {
        if (!v->string) continue;
        if (cJSON_IsArray(v)) {
            cfg_msg_t *m = calloc(1, sizeof(*m));
            if (!m) return;
            strlcpy(m->key, v->string, sizeof(m->key));
            cJSON *item;
            cJSON_ArrayForEach(item, v) {
                if (m->arr_len < FACE_COUNT && cJSON_IsNumber(item)) m->arr[m->arr_len++] = (uint8_t)item->valueint;
            }
            ui_post(cfg_apply, m);
        } else if (cJSON_IsBool(v) || cJSON_IsNumber(v)) {
            cfg_msg_t *m = calloc(1, sizeof(*m));
            if (!m) return;
            strlcpy(m->key, v->string, sizeof(m->key));
            m->value = cJSON_IsBool(v) ? cJSON_IsTrue(v) : (int)lround(v->valuedouble);
            ui_post(cfg_apply, m);
        }
    }
}

void more_hello(void)
{
    ui_post(start, NULL);
}

void more_gone(void)
{
    S.phone.known = false;
    cfg_sent[0] = 0;
    act_due = false;
    bus_emit(EVT_LINK, NULL);
}

/* ----------------------------------------------------------------- photos */

typedef struct {
    uint8_t *jpg;
    size_t len;
    uint32_t crc;
    char key[16];
} photo_job_t;

static volatile bool storing;

/* Checks one received photo and writes it to the library, then tells the
 * phone the name it was given. Its own task, because writing flash is slow
 * and must not be done on Bluetooth's task or from a stack in external RAM. */
static void photo_task(void *arg)
{
    photo_job_t *job = arg;
    char name[24] = "", extra[64];
    const char *why = link_crc32(job->jpg, job->len) == job->crc ? photo_add(job->jpg, job->len, name) : "lost";
    if (why) {
        ESP_LOGW(TAG, "photo %s refused: %s", job->key, why);
        snprintf(extra, sizeof(extra), ",\"why\":\"%s\"", why);
    } else {
        snprintf(extra, sizeof(extra), ",\"name\":\"%s\"", name);
    }
    link_blob_reply("photo", job->key, why == NULL, extra, true);
    free(job->jpg);
    free(job);
    storing = false;
    vTaskDelete(NULL);
}

void more_photo(uint8_t *jpg, size_t len, uint32_t crc, const char *key)
{
    photo_job_t *job = calloc(1, sizeof(*job));
    if (job) {
        job->jpg = jpg, job->len = len, job->crc = crc;
        strlcpy(job->key, key, sizeof(job->key));
        storing = true;
        if (xTaskCreate(photo_task, "photo", 6144, job, 2, NULL) == pdPASS) return;
        storing = false;
    }
    free(job);
    free(jpg);
    link_blob_reply("photo", key, false, ",\"why\":\"memory\"", false);
}

bool more_photo_busy(void)
{
    return storing;
}

static void photos_message(cJSON *msg)
{
    if (strcmp(str(msg, "n"), "delete") != 0) return;
    /* One small flash write; quick enough to do here. */
    if (!photo_remove(str(msg, "name"))) ESP_LOGW(TAG, "no photo called %.24s to delete", str(msg, "name"));
}

/* --------------------------------------------------------------- dispatch */

bool more_handle(const char *type, cJSON *msg)
{
    if (strcmp(type, "place") == 0) place_message(msg);
    else if (strcmp(type, "agenda") == 0) agenda_message(msg);
    else if (strcmp(type, "busy") == 0) busy_message(msg);
    else if (strcmp(type, "day") == 0) day_message(msg);
    else if (strcmp(type, "replies") == 0) replies_message(msg);
    else if (strcmp(type, "phone") == 0) phone_message(msg);
    else if (strcmp(type, "cfg") == 0) cfg_message(msg);
    else if (strcmp(type, "photos") == 0) photos_message(msg);
    else return false;
    return true;
}
