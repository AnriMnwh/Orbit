/* iPhone companion link.
 *
 * An iPhone exposes three services to a paired accessory, and the watch reads
 * them as a client on the connection the phone opened:
 *   ANCS  notifications and incoming calls
 *   AMS   what is playing, plus play/pause/skip/volume
 *   CTS   the current time and time zone
 * No app is needed on the phone.
 *
 * Written from Apple's published specifications; not yet run against a real
 * phone. Setup is one long chain of discover/subscribe steps: each step's
 * completion callback calls advance().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

#include "json_util.h"
#include "platform.h"

static const char *TAG = "apple";

enum {
    ST_IDLE, ST_ANCS_SVC, ST_ANCS_CHR, ST_SUB_DATA, ST_SUB_SOURCE, ST_AMS_SVC, ST_AMS_CHR, ST_SUB_ENTITY,
    ST_WANT_TRACK, ST_WANT_PLAYER, ST_CTS_SVC, ST_CTS_CHR, ST_READ_TIME, ST_READ_ZONE, ST_DONE,
};

static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static int state = ST_IDLE;
static bool have_ancs, have_ams;
static ble_uuid_any_t U_ANCS, U_SOURCE, U_CONTROL, U_DATA, U_AMS, U_REMOTE, U_ENTITY;
static uint16_t svc_start, svc_end;
static uint16_t h_source, h_control, h_data, h_remote, h_entity, h_time, h_zone;
static uint8_t time_raw[10];

/* Notification details are fetched one at a time. */
#define QUEUE 8
static struct {
    uint32_t uid;
    bool call;
} queue[QUEUE];
static int q_head, q_count;
static bool fetching;
static uint8_t data_buf[400];
static size_t data_len;
static uint32_t call_uid;
static bool call_ringing;

static void advance(void);

/* ------------------------------------------------------- LVGL-side updates */

enum { K_NOTIFY, K_REMOVE, K_CALL, K_MEDIA };
enum { M_TITLE = 1, M_ARTIST = 2, M_ALBUM = 4, M_DURATION = 8, M_STATE = 16, M_VOLUME = 32 };

typedef struct {
    int kind;
    notif_t notif;
    call_t call;
    media_t media;
    int media_mask;
} msg_t;

static void apply(void *arg)
{
    msg_t *m = arg;
    switch (m->kind) {
    case K_NOTIFY: notif_push(&m->notif); break;
    case K_REMOVE: notif_remove(m->notif.id); break;
    case K_CALL:
        S.call = m->call;
        bus_emit(EVT_CALL, NULL);
        break;
    case K_MEDIA:
        if (m->media_mask & M_TITLE) strlcpy(S.media.title, m->media.title, sizeof(S.media.title));
        if (m->media_mask & M_ARTIST) strlcpy(S.media.artist, m->media.artist, sizeof(S.media.artist));
        if (m->media_mask & M_ALBUM) strlcpy(S.media.album, m->media.album, sizeof(S.media.album));
        if (m->media_mask & M_DURATION) S.media.duration_s = m->media.duration_s;
        if (m->media_mask & M_VOLUME) S.media.volume = m->media.volume;
        if (m->media_mask & M_STATE) {
            S.media.playing = m->media.playing;
            S.media.position_s = m->media.position_s;
            S.media.stamp_ms = svc_millis();
        }
        if (m->media_mask & M_TITLE) S.media.position_s = 0, S.media.stamp_ms = svc_millis();
        S.media.valid = true;
        bus_emit(EVT_MEDIA, NULL);
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

/* ui_post leaves the message with us when the UI could not take it. */
static void post(msg_t *m)
{
    if (!ui_post(apply, m)) free(m);
}

/* ---------------------------------------------------------------- helpers */

static int wrote(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (arg) advance(); /* only setup writes carry the flag */
    return 0;
}

static void write_step(uint16_t handle, const void *data, size_t len)
{
    if (ble_gattc_write_flat(conn, handle, data, (uint16_t)len, wrote, (void *)1) != 0) advance();
}

static void subscribe(uint16_t value_handle)
{
    static const uint8_t on[2] = {1, 0};
    write_step(value_handle + 1, on, 2); /* the switch sits right after the value */
}

static int found_svc(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0 && svc) {
        svc_start = svc->start_handle, svc_end = svc->end_handle;
        return 0;
    }
    advance(); /* BLE_HS_EDONE, or the phone does not offer it */
    return 0;
}

static int found_chr(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0 && chr) {
        const ble_uuid_t *u = &chr->uuid.u;
        if (ble_uuid_cmp(u, &U_SOURCE.u) == 0) h_source = chr->val_handle;
        else if (ble_uuid_cmp(u, &U_CONTROL.u) == 0) h_control = chr->val_handle;
        else if (ble_uuid_cmp(u, &U_DATA.u) == 0) h_data = chr->val_handle;
        else if (ble_uuid_cmp(u, &U_REMOTE.u) == 0) h_remote = chr->val_handle;
        else if (ble_uuid_cmp(u, &U_ENTITY.u) == 0) h_entity = chr->val_handle;
        else if (ble_uuid_cmp(u, BLE_UUID16_DECLARE(0x2A2B)) == 0) h_time = chr->val_handle;
        else if (ble_uuid_cmp(u, BLE_UUID16_DECLARE(0x2A0F)) == 0) h_zone = chr->val_handle;
        return 0;
    }
    advance();
    return 0;
}

static void find_svc(const ble_uuid_t *uuid)
{
    svc_start = svc_end = 0;
    if (ble_gattc_disc_svc_by_uuid(conn, uuid, found_svc, NULL) != 0) advance();
}

static void find_chrs(void)
{
    if (!svc_start || ble_gattc_disc_all_chrs(conn, svc_start, svc_end, found_chr, NULL) != 0) advance();
}

static int read_done(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    uint8_t b[10] = {0};
    uint16_t len = 0;
    if (err->status == 0 && attr) ble_hs_mbuf_to_flat(attr->om, b, sizeof(b), &len);

    if (state == ST_READ_TIME) {
        memcpy(time_raw, b, sizeof(time_raw));
        time_raw[9] = len >= 7; /* reuse the last byte as "valid" */
    } else if (state == ST_READ_ZONE && time_raw[9]) {
        /* Phone time is local; the zone record says how far that is from UTC. */
        struct tm t = {.tm_year = (time_raw[0] | (time_raw[1] << 8)) - 1900, .tm_mon = time_raw[2] - 1,
                       .tm_mday = time_raw[3], .tm_hour = time_raw[4], .tm_min = time_raw[5], .tm_sec = time_raw[6]};
        /* Zone and daylight-saving offsets come in quarter hours; -128 and 255 mean "unknown". */
        int zone = (int8_t)b[0] == -128 ? 0 : (int8_t)b[0], dst = b[1] == 255 ? 0 : b[1];
        int offset = len >= 2 ? (zone + dst) * 15 : 0;
        clock_set(mktime(&t) - (time_t)offset * 60, offset, len >= 2);
    }
    advance();
    return 0;
}

static void read_step(uint16_t handle)
{
    if (!handle || ble_gattc_read(conn, handle, read_done, NULL) != 0) advance();
}

/* ------------------------------------------------------------ setup chain */

static void advance(void)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    state++;
    switch (state) {
    case ST_ANCS_SVC: find_svc(&U_ANCS.u); break;
    case ST_ANCS_CHR: find_chrs(); break;
    case ST_SUB_DATA:
        have_ancs = h_source && h_control && h_data;
        if (have_ancs) {
            ble_mark_iphone();
            subscribe(h_data);
        } else {
            advance();
        }
        break;
    case ST_SUB_SOURCE:
        if (have_ancs) subscribe(h_source);
        else advance();
        break;
    case ST_AMS_SVC: find_svc(&U_AMS.u); break;
    case ST_AMS_CHR: find_chrs(); break;
    case ST_SUB_ENTITY:
        have_ams = h_remote && h_entity;
        if (have_ams) subscribe(h_entity);
        else advance();
        break;
    case ST_WANT_TRACK: {
        static const uint8_t track[] = {2, 0, 1, 2, 3}; /* artist, album, title, duration */
        if (have_ams) write_step(h_entity, track, sizeof(track));
        else advance();
        break;
    }
    case ST_WANT_PLAYER: {
        static const uint8_t player[] = {0, 1, 2}; /* playback state, volume */
        if (have_ams) write_step(h_entity, player, sizeof(player));
        else advance();
        break;
    }
    case ST_CTS_SVC: find_svc(BLE_UUID16_DECLARE(0x1805)); break;
    case ST_CTS_CHR: find_chrs(); break;
    case ST_READ_TIME: read_step(h_time); break;
    case ST_READ_ZONE: read_step(h_zone); break;
    default:
        state = ST_DONE;
        ESP_LOGI(TAG, "iPhone services: notifications %s, media %s", have_ancs ? "yes" : "no", have_ams ? "yes" : "no");
        break;
    }
}

void apple_start(uint16_t c)
{
    if (state != ST_IDLE) return;
    conn = c;
    have_ancs = have_ams = false;
    h_source = h_control = h_data = h_remote = h_entity = h_time = h_zone = 0;
    q_head = q_count = 0, fetching = false, data_len = 0, call_ringing = false;
    memset(time_raw, 0, sizeof(time_raw));
    if (!U_ANCS.u.type) {
        ble_uuid_from_str(&U_ANCS, "7905f431-b5ce-4e99-a40f-4b1e122d00d0");
        ble_uuid_from_str(&U_SOURCE, "9fbf120d-6301-42d9-8c58-25e699a21dbd");
        ble_uuid_from_str(&U_CONTROL, "69d1d8f3-45e1-49a8-9821-9bbdfdaad9d9");
        ble_uuid_from_str(&U_DATA, "22eac6e9-24d6-4bb5-be44-b36ace7c7bfb");
        ble_uuid_from_str(&U_AMS, "89d3502b-0f36-433a-8ef4-c502ad55f8dc");
        ble_uuid_from_str(&U_REMOTE, "9b3c81d8-57b1-4a8a-b8df-0e56f7ca51c2");
        ble_uuid_from_str(&U_ENTITY, "2f7cabce-808d-411f-9a0c-bb92ba96c102");
    }
    advance();
}

void apple_stop(void)
{
    if (call_ringing) {
        msg_t *m = new_msg(K_CALL);
        if (m) post(m);
    }
    conn = BLE_HS_CONN_HANDLE_NONE;
    state = ST_IDLE;
    have_ancs = have_ams = false;
    call_ringing = false;
}

bool apple_active(void)
{
    return conn != BLE_HS_CONN_HANDLE_NONE && (have_ancs || have_ams);
}

/* ---------------------------------------------------------- notifications */

/* iOS can stop part-way through a details reply, which would leave `fetching`
 * set for good. A reply that is overdue is given up on so the queue moves on. */
#define FETCH_TIMEOUT_US (5 * 1000 * 1000)
static int64_t fetch_started_us;

static void fetch_next(void)
{
    if (fetching || q_count == 0 || !have_ancs) return;
    uint32_t uid = queue[q_head].uid;
    /* Ask for the app, a title of up to 47 bytes and a message of up to 159. */
    uint8_t cmd[] = {0, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24),
                     0, 1, 47, 0, 3, 159, 0};
    data_len = 0;
    fetching = ble_gattc_write_flat(conn, h_control, cmd, sizeof(cmd), wrote, NULL) == 0;
    if (fetching) fetch_started_us = esp_timer_get_time();
    else q_head = (q_head + 1) % QUEUE, q_count--;
}

static void fetch_expire(void)
{
    if (!fetching || esp_timer_get_time() - fetch_started_us < FETCH_TIMEOUT_US) return;
    ESP_LOGW(TAG, "notification details never completed; skipping it");
    fetching = false;
    q_head = (q_head + 1) % QUEUE, q_count--;
}

static void app_label(const char *bundle, char *out, size_t len)
{
    static const struct {
        const char *id, *name;
    } known[] = {
        {"com.apple.MobileSMS", "MESSAGES"}, {"com.apple.mobilemail", "MAIL"}, {"com.apple.mobilecal", "CALENDAR"},
        {"com.apple.mobilephone", "PHONE"}, {"com.apple.reminders", "REMINDERS"}, {"com.apple.facetime", "FACETIME"},
        {"net.whatsapp.WhatsApp", "WHATSAPP"}, {"com.burbn.instagram", "INSTAGRAM"}, {"com.google.Gmail", "GMAIL"},
        {"ph.telegra.Telegraph", "TELEGRAM"}, {"com.facebook.Messenger", "MESSENGER"}, {"com.tinyspeck.chatlyio", "SLACK"},
        {"com.hammerandchisel.discord", "DISCORD"}, {"com.spotify.client", "SPOTIFY"},
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strcmp(bundle, known[i].id) == 0) {
            strlcpy(out, known[i].name, len);
            return;
        }
    }
    /* Otherwise the last part of the identifier, in capitals. */
    const char *dot = strrchr(bundle, '.');
    strlcpy(out, dot ? dot + 1 : bundle, len);
    ascii_upper(out);
}

/* Returns true once the reply in data_buf is complete (and consumed). */
static bool parse_details(void)
{
    if (data_len < 5) return false;
    char fields[3][168] = {{0}};
    size_t pos = 5;
    for (int i = 0; i < 3; i++) {
        if (pos + 3 > data_len) return false;
        uint8_t id = data_buf[pos];
        size_t n = data_buf[pos + 1] | ((size_t)data_buf[pos + 2] << 8);
        pos += 3;
        if (pos + n > data_len) return false;
        /* Attributes are placed by their ANCS id (0 app, 1 title, 3 message), not by position. */
        int slot = id == 0 ? 0 : id == 1 ? 1 : id == 3 ? 2 : -1;
        if (slot >= 0) {
            size_t keep = n < sizeof(fields[0]) - 1 ? n : sizeof(fields[0]) - 1;
            memcpy(fields[slot], data_buf + pos, keep);
        }
        pos += n;
    }
    uint32_t uid = data_buf[1] | (data_buf[2] << 8) | (data_buf[3] << 16) | ((uint32_t)data_buf[4] << 24);
    bool is_call = q_count && queue[q_head].uid == uid && queue[q_head].call;

    if (is_call) {
        msg_t *m = new_msg(K_CALL);
        if (m) {
            m->call.active = true;
            strlcpy(m->call.who, fields[1], sizeof(m->call.who));
            post(m);
        }
    } else {
        msg_t *m = new_msg(K_NOTIFY);
        if (m) {
            m->notif.id = uid;
            m->notif.when = svc_time();
            app_label(fields[0], m->notif.app, sizeof(m->notif.app));
            strlcpy(m->notif.title, fields[1], sizeof(m->notif.title));
            strlcpy(m->notif.body, fields[2], sizeof(m->notif.body));
            post(m);
        }
    }
    return true;
}

static void on_source(const uint8_t *d, size_t len)
{
    if (len < 8) return;
    uint8_t event = d[0], flags = d[1], category = d[2];
    uint32_t uid = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24);
    bool is_call = category == 1;

    if (event == 2) { /* removed */
        if (call_ringing && uid == call_uid) {
            call_ringing = false;
            msg_t *m = new_msg(K_CALL);
            if (m) post(m);
        } else {
            msg_t *m = new_msg(K_REMOVE);
            if (m) {
                m->notif.id = uid;
                post(m);
            }
        }
        return;
    }
    /* Skip silent ones and the backlog iOS replays when it connects. */
    if (event != 0 || (flags & 0x05)) return;
    fetch_expire();
    if (is_call) call_uid = uid, call_ringing = true;
    if (q_count < QUEUE) {
        int slot = (q_head + q_count) % QUEUE;
        queue[slot].uid = uid, queue[slot].call = is_call;
        q_count++;
    }
    fetch_next();
}

static void on_data(const uint8_t *d, size_t len)
{
    if (!fetching) return;
    if (data_len + len > sizeof(data_buf)) len = sizeof(data_buf) - data_len;
    memcpy(data_buf + data_len, d, len);
    data_len += len;
    if (parse_details() || data_len >= sizeof(data_buf)) {
        fetching = false;
        q_head = (q_head + 1) % QUEUE, q_count--;
        fetch_next();
    }
}

void apple_notif_action(uint32_t uid, bool positive)
{
    if (!have_ancs) return;
    if (uid == 0) {
        if (!call_ringing) return;
        uid = call_uid;
    }
    uint8_t cmd[] = {2, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16), (uint8_t)(uid >> 24), positive ? 0 : 1};
    ble_gattc_write_flat(conn, h_control, cmd, sizeof(cmd), wrote, NULL);
}

/* ------------------------------------------------------------------ media */

static void on_entity(const uint8_t *d, size_t len)
{
    if (len < 3) return;
    char value[72] = {0};
    size_t n = len - 3 < sizeof(value) - 1 ? len - 3 : sizeof(value) - 1;
    memcpy(value, d + 3, n);
    msg_t *m = new_msg(K_MEDIA);
    if (!m) return;

    if (d[0] == 2) { /* track */
        if (d[1] == 0) m->media_mask = M_ARTIST, strlcpy(m->media.artist, value, sizeof(m->media.artist));
        else if (d[1] == 1) m->media_mask = M_ALBUM, strlcpy(m->media.album, value, sizeof(m->media.album));
        else if (d[1] == 2) m->media_mask = M_TITLE, strlcpy(m->media.title, value, sizeof(m->media.title));
        else if (d[1] == 3) m->media_mask = M_DURATION, m->media.duration_s = (int)strtod(value, NULL);
    } else if (d[0] == 0) { /* player */
        if (d[1] == 1) { /* "state,rate,elapsed": state 1 is playing */
            m->media_mask = M_STATE;
            m->media.playing = value[0] == '1';
            const char *comma = strrchr(value, ',');
            m->media.position_s = comma ? (int)strtod(comma + 1, NULL) : 0;
        } else if (d[1] == 2) {
            m->media_mask = M_VOLUME, m->media.volume = (int)(strtod(value, NULL) * 100.0 + 0.5);
        }
    }
    if (m->media_mask) post(m);
    else free(m);
}

void apple_media(media_cmd_t cmd)
{
    /* Apple's remote command numbers: 2 toggle, 3 next, 4 previous, 5/6 volume. */
    static const uint8_t code[] = {2, 3, 4, 5, 6};
    if (!have_ams) return;
    ble_gattc_write_flat(conn, h_remote, &code[cmd], 1, wrote, NULL);
}

int apple_on_notify(uint16_t c, uint16_t attr, const uint8_t *data, size_t len)
{
    if (c != conn) return 0;
    if (attr == h_source && have_ancs) on_source(data, len);
    else if (attr == h_data && have_ancs) on_data(data, len);
    else if (attr == h_entity && have_ams) on_entity(data, len);
    return 0;
}
