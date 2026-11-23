/* Android companion link, via the open-source Gadgetbridge app.
 *
 * Gadgetbridge is told the watch is a "Bangle.js" and then speaks that
 * watch's simple protocol over a Bluetooth serial link: the phone sends lines
 * of JavaScript such as  GB({"t":"notify",...})  or  setTime(1700000000);
 * and the watch answers with lines of JSON.
 *
 * Written from the published protocol; not yet run against a real phone.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#include "platform.h"

static const char *TAG = "gb";

static char line[1500];
static size_t line_len;
static bool overflowed;

/* ------------------------------------------------------- LVGL-side updates */

typedef struct {
    int kind;
    notif_t notif;
    media_t media;
    call_t call;
    gps_t gps;
    weather_t weather;
    uint32_t id;
    bool extras; /* musicstate carried the Orbit app's extra fields */
} msg_t;

enum { K_NOTIFY, K_UNNOTIFY, K_TRACK, K_PLAYSTATE, K_CALL, K_GPS, K_WEATHER, K_FIND };

static void apply(void *arg)
{
    msg_t *m = arg;
    switch (m->kind) {
    case K_NOTIFY: notif_push(&m->notif); break;
    case K_UNNOTIFY: notif_remove(m->id); break;
    case K_TRACK:
        strlcpy(S.media.title, m->media.title, sizeof(S.media.title));
        strlcpy(S.media.artist, m->media.artist, sizeof(S.media.artist));
        strlcpy(S.media.album, m->media.album, sizeof(S.media.album));
        strlcpy(S.media.app, m->media.app, sizeof(S.media.app));
        S.media.duration_s = m->media.duration_s;
        S.media.position_s = 0;
        S.media.stamp_ms = svc_millis();
        S.media.valid = true;
        bus_emit(EVT_MEDIA, NULL);
        break;
    case K_PLAYSTATE:
        S.media.playing = m->media.playing;
        if (m->media.position_s >= 0) S.media.position_s = m->media.position_s;
        if (m->media.volume >= 0) S.media.volume = m->media.volume;
        if (m->extras) {
            /* The Orbit phone app says what this player can do right now. */
            S.media.shuffle = m->media.shuffle, S.media.repeat = m->media.repeat;
            S.media.can_seek = m->media.can_seek, S.media.can_shuffle = m->media.can_shuffle;
            S.media.can_repeat = m->media.can_repeat, S.media.can_like = m->media.can_like;
        }
        S.media.stamp_ms = svc_millis();
        S.media.valid = true;
        bus_emit(EVT_MEDIA, NULL);
        break;
    case K_CALL:
        S.call = m->call;
        bus_emit(EVT_CALL, NULL);
        break;
    case K_GPS:
        S.gps = m->gps;
        S.gps.stamp_ms = svc_millis();
        bus_emit(EVT_GPS, NULL);
        break;
    case K_WEATHER:
        S.weather = m->weather;
        bus_emit(EVT_WEATHER, NULL);
        break;
    case K_FIND:
        /* "Find my watch" from the phone. */
        if (m->id) {
            svc_sound(SND_ALARM);
            shell_alert("Here I am", "Your phone is looking for this watch.");
        }
        break;
    }
    free(m);
}

static msg_t *new_msg(int kind)
{
    msg_t *m = calloc(1, sizeof(*m));
    if (m) m->kind = kind;
    return m;
}

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

static void upper(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    }
}

/* ---------------------------------------------------------------- parsing */

static void handle_json(cJSON *j)
{
    const char *t = str(j, "t");
    msg_t *m = NULL;

    /* Messages only the Orbit phone app sends (hello, pictures, assistant). */
    if (link_handle(t, j)) return;

    if (strcmp(t, "notify") == 0) {
        if (!(m = new_msg(K_NOTIFY))) return;
        m->notif.id = (uint32_t)num(j, "id", 0);
        m->notif.when = svc_time();
        strlcpy(m->notif.app, str(j, "src"), sizeof(m->notif.app));
        upper(m->notif.app);
        const char *title = str(j, "title");
        if (!title[0]) title = str(j, "sender");
        if (!title[0]) title = str(j, "subject");
        strlcpy(m->notif.title, title, sizeof(m->notif.title));
        strlcpy(m->notif.body, str(j, "body"), sizeof(m->notif.body));
        m->notif.no_reply = cJSON_IsFalse(cJSON_GetObjectItem(j, "reply"));
    } else if (strcmp(t, "notify-") == 0) {
        if (!(m = new_msg(K_UNNOTIFY))) return;
        m->id = (uint32_t)num(j, "id", 0);
    } else if (strcmp(t, "musicinfo") == 0) {
        if (!(m = new_msg(K_TRACK))) return;
        strlcpy(m->media.title, str(j, "track"), sizeof(m->media.title));
        strlcpy(m->media.artist, str(j, "artist"), sizeof(m->media.artist));
        strlcpy(m->media.album, str(j, "album"), sizeof(m->media.album));
        m->media.duration_s = (int)num(j, "dur", 0);
        strlcpy(m->media.app, str(j, "app"), sizeof(m->media.app));
        /* "art" names this track's picture; the picture itself follows separately. */
        cJSON *art = cJSON_GetObjectItem(j, "art");
        if (cJSON_IsString(art)) link_art_key(art->valuestring);
    } else if (strcmp(t, "musicstate") == 0) {
        if (!(m = new_msg(K_PLAYSTATE))) return;
        m->media.playing = strcmp(str(j, "state"), "play") == 0;
        m->media.position_s = (int)num(j, "position", -1);
        m->media.volume = (int)num(j, "vol", -1);
        cJSON *can = cJSON_GetObjectItem(j, "can"), *c;
        if (cJSON_IsArray(can)) {
            m->extras = true;
            m->media.shuffle = num(j, "shuffle", 0) != 0;
            m->media.repeat = num(j, "repeat", 0) != 0;
            cJSON_ArrayForEach(c, can) {
                const char *what = cJSON_IsString(c) ? c->valuestring : "";
                if (strcmp(what, "seek") == 0) m->media.can_seek = true;
                else if (strcmp(what, "shuffle") == 0) m->media.can_shuffle = true;
                else if (strcmp(what, "repeat") == 0) m->media.can_repeat = true;
                else if (strcmp(what, "like") == 0) m->media.can_like = true;
            }
        }
    } else if (strcmp(t, "call") == 0) {
        if (!(m = new_msg(K_CALL))) return;
        m->call.active = strcmp(str(j, "cmd"), "incoming") == 0;
        const char *who = str(j, "name");
        strlcpy(m->call.who, who[0] ? who : str(j, "number"), sizeof(m->call.who));
    } else if (strcmp(t, "gps") == 0) {
        if (!(m = new_msg(K_GPS))) return;
        m->gps.lat = num(j, "lat", NAN);
        m->gps.lon = num(j, "lon", NAN);
        m->gps.speed_kmh = (float)num(j, "speed", 0);
        m->gps.course = (float)num(j, "course", 0);
        m->gps.fix = !isnan(m->gps.lat) && !isnan(m->gps.lon);
    } else if (strcmp(t, "weather") == 0) {
        if (!(m = new_msg(K_WEATHER))) return;
        /* Gadgetbridge sends kelvin. */
        m->weather.valid = true;
        m->weather.temp_c = (int)lround(num(j, "temp", 273.15) - 273.15);
        m->weather.hi_c = (int)lround(num(j, "hi", 273.15) - 273.15);
        m->weather.lo_c = (int)lround(num(j, "lo", 273.15) - 273.15);
        strlcpy(m->weather.text, str(j, "txt"), sizeof(m->weather.text));
    } else if (strcmp(t, "find") == 0) {
        if (!(m = new_msg(K_FIND))) return;
        m->id = cJSON_IsTrue(cJSON_GetObjectItem(j, "n"));
    } else if (strcmp(t, "is_gps_active") == 0) {
        gb_send("{\"t\":\"gps_power\",\"status\":false}");
        return;
    } else {
        return;
    }
    ui_post(apply, m);
}

static void handle_line(char *s)
{
    /* Clock: "setTime(1700000000);E.setTimeZone(11);..." */
    char *p = strstr(s, "setTime(");
    if (p) {
        double utc = strtod(p + 8, NULL);
        char *z = strstr(s, "setTimeZone(");
        double hours = z ? strtod(z + 12, NULL) : 0;
        clock_set((time_t)utc, (int)lround(hours * 60.0), z != NULL);
    }
    /* Messages: GB({...}) */
    p = strstr(s, "GB(");
    char *end = p ? strrchr(p, ')') : NULL;
    if (!p || !end) return;
    *end = 0;
    cJSON *j = cJSON_Parse(p + 3);
    if (j) handle_json(j);
    else ESP_LOGW(TAG, "unreadable message: %.80s", p + 3);
    cJSON_Delete(j);
}

void gb_receive(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\n') {
            line[line_len] = 0;
            if (!overflowed && line_len) handle_line(line);
            line_len = 0, overflowed = false;
        } else if (c >= 0x20) { /* skip control bytes such as the leading 0x10 */
            if (line_len < sizeof(line) - 1) line[line_len++] = (char)c;
            else overflowed = true;
        }
    }
}

void gb_connected(bool up)
{
    line_len = 0, overflowed = false;
    if (!up) return;
    char json[96];
    snprintf(json, sizeof(json), "{\"t\":\"ver\",\"fw\":\"Orbit %s\",\"hw\":\"Orbit\"}", ORBIT_VERSION);
    gb_send(json);
    snprintf(json, sizeof(json), "{\"t\":\"status\",\"bat\":%d,\"chg\":%d}", S.batt.percent < 0 ? 100 : S.batt.percent,
             S.batt.charging ? 1 : 0);
    gb_send(json);
}
