/* Bluetooth LE.
 *
 * The watch is one of two devices at a time, each with its own address so
 * hosts never confuse them:
 *
 *   "Orbit"       phone companion. iPhones are read through Apple's own
 *                 notification / media / time services (ble_apple.c). Android
 *                 phones talk to it through Gadgetbridge (ble_gb.c).
 *   "Orbit Keys"  a Bluetooth keyboard + media remote for a computer.
 *
 * They are kept apart because a phone that sees a keyboard hides its
 * on-screen keyboard. Only the services of the active device are visible.
 */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "platform.h"

static const char *TAG = "ble";

void ble_store_config_init(void);

#define NAME_PHONE "Orbit"
#define NAME_KEYS "Orbit Keys"
#define APPEARANCE_WATCH 0x00C0
#define APPEARANCE_KEYBOARD 0x03C1

static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static bool synced, keys_mode, want_keys, encrypted, gb_seen, app_mode;
/* The battery as the UI last reported it, for Bluetooth's task to read. */
static volatile int batt_pct = -1;
static volatile bool batt_chg, batt_sub; /* batt_sub: the phone listens for battery changes */
static peer_kind_t peer = PEER_NONE;
static char peer_name[32];
static uint8_t keys_addr[6];
static esp_timer_handle_t secure_timer;
static QueueHandle_t hid_jobs;

static ble_uuid_any_t U_NUS, U_NUS_RX, U_NUS_TX, U_ANCS, U_BULK, U_BULK_RX, U_BULK_TX;
static uint16_t nus_tx_handle, kbd_handle, consumer_handle, batt_handle, bulk_tx_handle;
static uint16_t nus_svc, hid_svc, bulk_svc;

/* ------------------------------------------------------------- UI updates */

static void link_apply(void *arg)
{
    link_t *l = arg;
    S.link = *l;
    free(l);
    bus_emit(EVT_LINK, NULL);
}

static void publish(void)
{
    link_t *l = calloc(1, sizeof(*l));
    if (!l) return;
    l->status = conn != BLE_HS_CONN_HANDLE_NONE ? LINK_CONNECTED : (synced ? LINK_WAITING : LINK_OFF);
    l->peer = conn != BLE_HS_CONN_HANDLE_NONE ? (keys_mode ? PEER_KEYS : peer) : PEER_NONE;
    l->keys_mode = want_keys;
    l->app = app_mode && conn != BLE_HS_CONN_HANDLE_NONE;
    if (conn != BLE_HS_CONN_HANDLE_NONE) strlcpy(l->name, peer_name, sizeof(l->name));
    if (!ui_post(link_apply, l)) free(l);
}

bool ble_phone_is_android(void)
{
    return conn != BLE_HS_CONN_HANDLE_NONE && !keys_mode && peer == PEER_ANDROID;
}

/* -------------------------------------------------------------- HID data */

/* Report 1: keyboard (modifiers, reserved, six keys). Report 2: one 16-bit
 * consumer usage (play/pause, volume and so on). */
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};
static const uint8_t HID_INFO[] = {0x11, 0x01, 0x00, 0x02};       /* HID 1.11, normally connectable */
static const uint8_t PNP_ID[] = {0x02, 0x3A, 0x30, 0x52, 0x4F, 0x00, 0x01}; /* USB vendor id of Espressif */
static uint8_t protocol_mode = 1;

enum { HID_INFO_CHR, HID_MAP_CHR, HID_CTRL_CHR, HID_PROTO_CHR, HID_KBD_IN, HID_KBD_OUT, HID_CONSUMER_IN };
static const uint8_t REF_KBD_IN[] = {1, 1}, REF_KBD_OUT[] = {1, 2}, REF_CONSUMER_IN[] = {2, 1};

static int reply(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len)
{
    return os_mbuf_append(ctxt->om, data, (uint16_t)len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int hid_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    static const uint8_t zeros[8] = {0};
    bool read = ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR;
    switch ((intptr_t)arg) {
    case HID_INFO_CHR: return reply(ctxt, HID_INFO, sizeof(HID_INFO));
    case HID_MAP_CHR: return reply(ctxt, REPORT_MAP, sizeof(REPORT_MAP));
    case HID_PROTO_CHR:
        if (read) return reply(ctxt, &protocol_mode, 1);
        ble_hs_mbuf_to_flat(ctxt->om, &protocol_mode, 1, NULL);
        return 0;
    case HID_KBD_IN: return reply(ctxt, zeros, 8);
    case HID_CONSUMER_IN: return reply(ctxt, zeros, 2);
    case HID_KBD_OUT: return read ? reply(ctxt, zeros, 1) : 0; /* keyboard LEDs: accepted, unused */
    default: return 0;
    }
}

static int ref_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return reply(ctxt, arg, 2);
}

static int info_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if ((intptr_t)arg == 0) return reply(ctxt, PNP_ID, sizeof(PNP_ID));
    if ((intptr_t)arg == 1) return reply(ctxt, "Orbit", 5);
    int percent = batt_pct;
    uint8_t level = (uint8_t)(percent < 0 ? 100 : percent);
    return reply(ctxt, &level, 1);
}

/* Gadgetbridge's serial link (Nordic UART service). */
static int nus_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    static uint8_t buf[520];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (!gb_seen) {
        gb_seen = true;
        peer = PEER_ANDROID;
        if (!peer_name[0]) strlcpy(peer_name, "Android phone", sizeof(peer_name));
        publish();
    }
    gb_receive(buf, len);
    return 0;
}

static bool notify(uint16_t handle, const void *data, size_t len);
static int bulk_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);

#define ENC_READ (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)

static const struct ble_gatt_svc_def SERVICES[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_NUS.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = &U_NUS_RX.u, .access_cb = nus_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = &U_NUS_TX.u, .access_cb = nus_access, .val_handle = &nus_tx_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = hid_access, .arg = (void *)HID_INFO_CHR, .flags = ENC_READ},
         {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = hid_access, .arg = (void *)HID_MAP_CHR, .flags = ENC_READ},
         {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = hid_access, .arg = (void *)HID_CTRL_CHR,
          .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_access, .arg = (void *)HID_PROTO_CHR,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)HID_KBD_IN, .val_handle = &kbd_handle,
          .flags = ENC_READ | BLE_GATT_CHR_F_NOTIFY,
          .descriptors = (struct ble_gatt_dsc_def[]){
              {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = ref_access, .arg = (void *)REF_KBD_IN},
              {0}}},
         {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)HID_KBD_OUT,
          .flags = ENC_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
          .descriptors = (struct ble_gatt_dsc_def[]){
              {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = ref_access, .arg = (void *)REF_KBD_OUT},
              {0}}},
         {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .arg = (void *)HID_CONSUMER_IN,
          .val_handle = &consumer_handle, .flags = ENC_READ | BLE_GATT_CHR_F_NOTIFY,
          .descriptors = (struct ble_gatt_dsc_def[]){
              {.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ, .access_cb = ref_access, .arg = (void *)REF_CONSUMER_IN},
              {0}}},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_BULK.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = &U_BULK_RX.u, .access_cb = bulk_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
         {.uuid = &U_BULK_TX.u, .access_cb = bulk_access, .val_handle = &bulk_tx_handle, .flags = BLE_GATT_CHR_F_NOTIFY},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180A),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = info_access, .arg = (void *)0, .flags = BLE_GATT_CHR_F_READ},
         {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = info_access, .arg = (void *)1, .flags = BLE_GATT_CHR_F_READ},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = info_access, .arg = (void *)2, .val_handle = &batt_handle,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
         {0}}},
    {0},
};

/* HID reports go out from the keyboard task, which can afford to wait a moment
 * when Bluetooth has no buffer free. Returns false if the report was lost. */
static bool notify(uint16_t handle, const void *data, size_t len)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE) return false;
    int rc = BLE_HS_ENOMEM;
    for (int tries = 0; tries < 20; tries++) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
        rc = om ? ble_gatts_notify_custom(conn, handle, om) : BLE_HS_ENOMEM;
        if (rc == 0) return true;
        if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) break;
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    ESP_LOGW(TAG, "key report not sent (%d)", rc);
    return false;
}

/* ---- paced sending for the phone link ----
 * Long lines and streams of bulk frames can outrun the radio. They are queued
 * and a small task feeds them out, waiting whenever Bluetooth has no room. */

typedef struct {
    uint16_t handle, len;
    uint8_t *data;
} tx_item_t;

static QueueHandle_t tx_queue;

/* One item is one whole line or one bulk frame, so two tasks sending at once
 * can never interleave. `tail` is the line's closing newline, or 0 for none.
 * Only tasks other than Bluetooth's own and the screen's may wait for room. */
static void tx_push(uint16_t handle, const void *data, size_t len, char tail, bool wait)
{
    size_t total = len + (tail ? 1 : 0);
    if (!tx_queue || conn == BLE_HS_CONN_HANDLE_NONE || !len || total > UINT16_MAX) return;
    tx_item_t item = {.handle = handle, .len = (uint16_t)total, .data = malloc(total)};
    if (!item.data) {
        ESP_LOGW(TAG, "no memory to send %u bytes", (unsigned)total);
        return;
    }
    memcpy(item.data, data, len);
    if (tail) item.data[len] = (uint8_t)tail;
    if (xQueueSend(tx_queue, &item, wait ? pdMS_TO_TICKS(4000) : 0) != pdTRUE) {
        ESP_LOGW(TAG, "send queue full: dropped %u bytes", (unsigned)total);
        free(item.data);
    }
}

static void tx_task(void *arg)
{
    LV_UNUSED(arg);
    tx_item_t item;
    for (;;) {
        if (xQueueReceive(tx_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        /* A long line goes out as several packets; a bulk frame always fits one. */
        for (size_t off = 0; off < item.len;) {
            size_t n = item.len - off, room = (size_t)ble_payload();
            if (n > room) n = room;
            bool sent = false;
            for (int tries = 0; tries < 400 && !sent && conn != BLE_HS_CONN_HANDLE_NONE; tries++) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(item.data + off, (uint16_t)n);
                sent = om && ble_gatts_notify_custom(conn, item.handle, om) == 0;
                if (!sent) vTaskDelay(pdMS_TO_TICKS(8)); /* no buffer free yet: let the radio drain */
            }
            if (!sent) {
                if (conn != BLE_HS_CONN_HANDLE_NONE) {
                    ESP_LOGW(TAG, "radio stayed full: dropped the last %u bytes of a %u byte message", (unsigned)(item.len - off),
                             (unsigned)item.len);
                }
                break;
            }
            off += n;
        }
        free(item.data);
    }
}

/* The UI's battery reading changed (LVGL thread). Bluetooth's own task reads
 * the copy kept here instead of `S`, and a connected phone subscribed to the
 * Battery Service is told. */
void ble_battery(int percent, bool charging)
{
    bool changed = percent != batt_pct;
    batt_pct = percent, batt_chg = charging;
    if (changed && batt_sub) {
        uint8_t level = (uint8_t)(percent < 0 ? 100 : percent);
        tx_push(batt_handle, &level, 1, 0, false);
    }
}

void ble_battery_get(int *percent, bool *charging)
{
    *percent = batt_pct, *charging = batt_chg;
}

/* Album art and speech from the Orbit phone app arrive here as binary frames. */
static int bulk_access(uint16_t c, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    static uint8_t frame[520];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, frame, sizeof(frame), &len) == 0) link_bulk_rx(frame, len);
    return 0;
}

void gb_send(const char *json)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE || keys_mode || !gb_seen) return;
    tx_push(nus_tx_handle, json, strlen(json), '\n', false);
}

/* The same, for a line that must not be lost behind a stream of bulk frames. */
void gb_send_wait(const char *json)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE || keys_mode || !gb_seen) return;
    tx_push(nus_tx_handle, json, strlen(json), '\n', true);
}

void ble_tx_bulk(const uint8_t *frame, size_t len, bool wait)
{
    if (conn == BLE_HS_CONN_HANDLE_NONE || keys_mode) return;
    tx_push(bulk_tx_handle, frame, len, 0, wait);
}

int ble_payload(void)
{
    int mtu = conn != BLE_HS_CONN_HANDLE_NONE ? ble_att_mtu(conn) : 23;
    return mtu > 23 ? mtu - 3 : 20;
}

void ble_request_pairing(void)
{
    if (conn != BLE_HS_CONN_HANDLE_NONE && !encrypted) ble_gap_security_initiate(conn);
}

bool ble_encrypted(void)
{
    return conn != BLE_HS_CONN_HANDLE_NONE && encrypted;
}

void ble_set_app(bool on)
{
    app_mode = on;
    if (on && !peer_name[0]) strlcpy(peer_name, "Android phone", sizeof(peer_name));
    publish();
}

/* ------------------------------------------------------------ HID sending */

typedef struct {
    uint8_t kind; /* 0 keys, 1 consumer, 2 text */
    uint8_t mods, key;
    uint16_t usage;
    char text[48];
} hid_job_t;

static void tap(uint8_t mods, uint8_t key)
{
    uint8_t report[8] = {mods, 0, key, 0, 0, 0, 0, 0};
    notify(kbd_handle, report, 8);
    vTaskDelay(pdMS_TO_TICKS(18));
    memset(report, 0, 8);
    notify(kbd_handle, report, 8);
    vTaskDelay(pdMS_TO_TICKS(12));
}

/* US keyboard layout: ASCII -> key code and whether Shift is held. */
static bool ascii_key(char ch, uint8_t *mods, uint8_t *key)
{
    static const char plain[] = "-=[]\\ ;'`,./";
    static const char shifted[] = "_+{}| :\"~<>?";
    static const char digits_shifted[] = ")!@#$%^&*(";
    *mods = 0;
    if (ch >= 'a' && ch <= 'z') *key = (uint8_t)(0x04 + ch - 'a');
    else if (ch >= 'A' && ch <= 'Z') *key = (uint8_t)(0x04 + ch - 'A'), *mods = 2;
    else if (ch >= '1' && ch <= '9') *key = (uint8_t)(0x1E + ch - '1');
    else if (ch == '0') *key = 0x27;
    else if (ch == ' ') *key = 0x2C;
    else if (ch == '\n') *key = 0x28;
    else if (ch == '\t') *key = 0x2B;
    else {
        const char *p;
        if ((p = strchr(digits_shifted, ch))) {
            int d = (int)(p - digits_shifted);
            *key = (uint8_t)(d == 0 ? 0x27 : 0x1E + d - 1), *mods = 2;
        } else if ((p = strchr(plain, ch)) && ch != ' ') {
            *key = (uint8_t)(0x2D + (p - plain));
        } else if ((p = strchr(shifted, ch)) && ch != ' ') {
            *key = (uint8_t)(0x2D + (p - shifted)), *mods = 2;
        } else {
            return false;
        }
    }
    return true;
}

static void hid_task(void *arg)
{
    LV_UNUSED(arg);
    hid_job_t j;
    for (;;) {
        if (xQueueReceive(hid_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        if (!keys_mode || conn == BLE_HS_CONN_HANDLE_NONE) continue;
        if (j.kind == 0) {
            tap(j.mods, j.key);
        } else if (j.kind == 1) {
            uint8_t r[2] = {(uint8_t)(j.usage & 0xFF), (uint8_t)(j.usage >> 8)};
            notify(consumer_handle, r, 2);
            vTaskDelay(pdMS_TO_TICKS(18));
            r[0] = r[1] = 0;
            notify(consumer_handle, r, 2);
        } else {
            for (const char *p = j.text; *p; p++) {
                uint8_t mods, key;
                if (ascii_key(*p, &mods, &key)) tap(mods, key);
            }
        }
    }
}

void hid_keys(uint8_t mods, uint8_t key)
{
    /* The modifier bits used by the macro editor: 1 ctrl, 2 shift, 4 alt, 8 gui
     * are exactly the left-hand modifier bits of a HID keyboard report. */
    hid_job_t j = {.kind = 0, .mods = mods, .key = key};
    if (hid_jobs) xQueueSend(hid_jobs, &j, 0);
}

void hid_consumer(uint16_t usage)
{
    hid_job_t j = {.kind = 1, .usage = usage};
    if (hid_jobs) xQueueSend(hid_jobs, &j, 0);
}

void hid_text(const char *text)
{
    hid_job_t j = {.kind = 2};
    strlcpy(j.text, text, sizeof(j.text));
    if (hid_jobs) xQueueSend(hid_jobs, &j, 0);
}

/* ------------------------------------------------------------ advertising */

static int on_gap(struct ble_gap_event *ev, void *arg);

static const char *persona_name(void)
{
    return keys_mode ? NAME_KEYS : NAME_PHONE;
}

/* Become one of the two devices: only its services are visible, and the
 * keyboard uses its own address. */
static void apply_persona(void)
{
    keys_mode = want_keys;
    ble_gatts_svc_set_visibility(nus_svc, !keys_mode);
    ble_gatts_svc_set_visibility(bulk_svc, !keys_mode);
    ble_gatts_svc_set_visibility(hid_svc, keys_mode);
    if (keys_mode) ble_hs_id_set_rnd(keys_addr);
    ble_svc_gap_device_name_set(persona_name());
    ble_svc_gap_device_appearance_set(keys_mode ? APPEARANCE_KEYBOARD : APPEARANCE_WATCH);
}

static void advertise(void)
{
    if (!synced || conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    apply_persona();

    uint8_t adv[31], rsp[31];
    size_t n = 0;
    uint16_t appearance = keys_mode ? APPEARANCE_KEYBOARD : APPEARANCE_WATCH;
    adv[n++] = 2, adv[n++] = 0x01, adv[n++] = 0x06; /* general discoverable, LE only */
    adv[n++] = 3, adv[n++] = 0x19, adv[n++] = (uint8_t)(appearance & 0xFF), adv[n++] = (uint8_t)(appearance >> 8);
    if (keys_mode) {
        adv[n++] = 3, adv[n++] = 0x03, adv[n++] = 0x12, adv[n++] = 0x18; /* HID service */
    } else {
        /* Tells iPhones we would like their notification service. */
        adv[n++] = 17, adv[n++] = 0x15;
        memcpy(adv + n, U_ANCS.u128.value, 16);
        n += 16;
    }
    const char *name = persona_name();
    size_t name_len = strlen(name);
    rsp[0] = (uint8_t)(name_len + 1), rsp[1] = 0x09;
    memcpy(rsp + 2, name, name_len);

    ble_gap_adv_set_data(adv, (int)n);
    ble_gap_adv_rsp_set_data(rsp, (int)(name_len + 2));

    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
                                   .itvl_min = 0x00A0, .itvl_max = 0x00F0};
    uint8_t own = keys_mode ? BLE_OWN_ADDR_RANDOM : BLE_OWN_ADDR_PUBLIC;
    int rc = ble_gap_adv_start(own, NULL, BLE_HS_FOREVER, &p, on_gap, NULL);
    if (rc != 0) ESP_LOGW(TAG, "advertising failed: %d", rc);
    publish();
}

/* ----------------------------------------------------------------- events */

static int name_read(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status == 0 && attr && c == conn) {
        uint16_t len = 0;
        char name[32] = {0};
        ble_hs_mbuf_to_flat(attr->om, name, sizeof(name) - 1, &len);
        if (len) {
            strlcpy(peer_name, name, sizeof(peer_name));
            publish();
        }
    }
    return 0;
}

/* Ask to pair unless the other side turned out to be Gadgetbridge, which
 * neither needs nor expects it. */
static void secure_cb(void *arg)
{
    LV_UNUSED(arg);
    if (conn == BLE_HS_CONN_HANDLE_NONE || encrypted || gb_seen) return;
    ble_gap_security_initiate(conn);
}

static int on_gap(struct ble_gap_event *ev, void *arg)
{
    struct ble_gap_conn_desc desc;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            advertise();
            break;
        }
        if (conn != BLE_HS_CONN_HANDLE_NONE) {
            /* The watch serves one phone at a time: a second link must not take over the first one's state. */
            ble_gap_terminate(ev->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        conn = ev->connect.conn_handle;
        encrypted = false, gb_seen = false, batt_sub = false;
        peer = keys_mode ? PEER_KEYS : PEER_NONE;
        peer_name[0] = 0;
        publish();
        esp_timer_stop(secure_timer);
        esp_timer_start_once(secure_timer, 1500 * 1000);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        if (ev->disconnect.conn.conn_handle != conn) break; /* not the link we are serving */
        conn = BLE_HS_CONN_HANDLE_NONE;
        encrypted = false, gb_seen = false, app_mode = false, batt_sub = false;
        apple_stop();
        link_reset();
        gb_connected(false);
        publish();
        advertise();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.conn_handle != conn || ev->enc_change.status != 0) break;
        encrypted = true;
        link_encrypted(); /* the Orbit app's hello may be waiting for this */
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A00), name_read, NULL);
        if (!keys_mode) apple_start(conn);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The other side forgot us: drop our old keys and pair afresh. */
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc) == 0) ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.conn_handle != conn) break;
        if (ev->subscribe.attr_handle == nus_tx_handle) gb_connected(ev->subscribe.cur_notify);
        else if (ev->subscribe.attr_handle == batt_handle) batt_sub = ev->subscribe.cur_notify;
        break;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        static uint8_t buf[512];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(ev->notify_rx.om, buf, sizeof(buf), &len) == 0) {
            apple_on_notify(ev->notify_rx.conn_handle, ev->notify_rx.attr_handle, buf, len);
        }
        break;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    default: break;
    }
    return 0;
}

/* iPhone found: called by ble_apple.c once Apple's services are there. */
void ble_mark_iphone(void)
{
    peer = PEER_IPHONE;
    if (!peer_name[0]) strlcpy(peer_name, "iPhone", sizeof(peer_name));
    publish();
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    /* A fixed random address for the keyboard, derived from the chip's own. */
    esp_read_mac(keys_addr, ESP_MAC_BT);
    keys_addr[0] ^= 0x5A;
    keys_addr[5] |= 0xC0;
    ble_hs_id_set_rnd(keys_addr);

    ble_gatts_find_svc(&U_NUS.u, &nus_svc);
    ble_gatts_find_svc(BLE_UUID16_DECLARE(0x1812), &hid_svc);
    ble_gatts_find_svc(&U_BULK.u, &bulk_svc);
    synced = true;
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "controller reset: %d", reason);
    synced = false;
}

static void host_task(void *arg)
{
    LV_UNUSED(arg);
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* -------------------------------------------------------------------- API */

static void switch_persona(void);

void svc_notif_reply(uint32_t id, const char *text)
{
    if (!ble_phone_is_android()) return;
    /* The text goes inside a JSON string, so quotes and backslashes are
     * escaped, and it is cut only between whole UTF-8 characters: half a
     * character would make the phone reject the entire line. */
    char safe[96], json[176];
    size_t n = 0;
    for (const char *p = text; *p;) {
        unsigned char c = (unsigned char)*p;
        size_t chars = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1)));
        if (c < 0x20) {
            p++;
            continue;
        }
        if (strnlen(p, chars) < chars) break; /* the text ends inside a character */
        size_t need = chars + (c == '"' || c == '\\' ? 1 : 0);
        if (n + need > sizeof(safe) - 1) break;
        if (need > chars) safe[n++] = '\\';
        memcpy(safe + n, p, chars);
        n += chars, p += chars;
    }
    safe[n] = 0;
    snprintf(json, sizeof(json), "{\"t\":\"notify\",\"n\":\"REPLY\",\"id\":%u,\"msg\":\"%s\"}", (unsigned)id, safe);
    gb_send(json);
}

void svc_link_keys_mode(bool on)
{
    want_keys = on;
    switch_persona();
}

static void switch_persona(void)
{
    if (!synced) return;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM); /* advertising restarts on disconnect */
    } else {
        if (ble_gap_adv_active()) ble_gap_adv_stop();
        advertise();
    }
    publish();
}

void svc_link_forget(void)
{
    ble_store_clear();
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

void svc_media(media_cmd_t cmd)
{
    static const uint16_t usage[] = {0x00CD, 0x00B5, 0x00B6, 0x00E9, 0x00EA};
    static const char *const gb[] = {"play", "next", "previous", "volumeup", "volumedown", "shuffle", "repeat", "like"};
    if (cmd > MEDIA_VOL_DOWN) {
        /* Shuffle, repeat and like exist only with the Orbit phone app. */
        if (!app_mode || !ble_phone_is_android()) return;
        char json[48];
        snprintf(json, sizeof(json), "{\"t\":\"music\",\"n\":\"%s\"}", gb[cmd]);
        gb_send(json);
    } else if (keys_mode) {
        hid_consumer(usage[cmd]);
    } else if (apple_active()) {
        apple_media(cmd);
    } else if (app_mode && ble_phone_is_android() && S.media.volume >= 0 && (cmd == MEDIA_VOL_UP || cmd == MEDIA_VOL_DOWN)) {
        /* The app takes an exact level, so the watch's number stays in step. */
        char json[48];
        int v = S.media.volume + (cmd == MEDIA_VOL_UP ? 7 : -7);
        snprintf(json, sizeof(json), "{\"t\":\"music\",\"n\":\"vol\",\"v\":%d}", v < 0 ? 0 : (v > 100 ? 100 : v));
        gb_send(json);
    } else if (ble_phone_is_android()) {
        char json[48];
        /* Gadgetbridge has separate play and pause commands. */
        const char *n = cmd == MEDIA_TOGGLE ? (S.media.playing ? "pause" : "play") : gb[cmd];
        snprintf(json, sizeof(json), "{\"t\":\"music\",\"n\":\"%s\"}", n);
        gb_send(json);
    }
}

void svc_media_seek(int seconds)
{
    if (!app_mode || !ble_phone_is_android()) return;
    char json[48];
    snprintf(json, sizeof(json), "{\"t\":\"music\",\"n\":\"seek\",\"pos\":%d}", seconds < 0 ? 0 : seconds);
    gb_send(json);
}

void svc_hid_keys(uint8_t mods, uint8_t key)
{
    hid_keys(mods, key);
}

void svc_hid_text(const char *text)
{
    hid_text(text);
}

void svc_hid_consumer(uint16_t usage)
{
    hid_consumer(usage);
}

void svc_find_phone(bool on)
{
    gb_send(on ? "{\"t\":\"findPhone\",\"n\":true}" : "{\"t\":\"findPhone\",\"n\":false}");
}

void svc_gps_want(bool on)
{
    gb_send(on ? "{\"t\":\"gps_power\",\"status\":true}" : "{\"t\":\"gps_power\",\"status\":false}");
}

void svc_notif_dismiss(uint32_t id)
{
    if (apple_active()) {
        apple_notif_action(id, false);
    } else if (ble_phone_is_android()) {
        char json[64];
        snprintf(json, sizeof(json), "{\"t\":\"notify\",\"n\":\"DISMISS\",\"id\":%u}", (unsigned)id);
        gb_send(json);
    }
}

void svc_call(bool accept)
{
    if (apple_active()) apple_notif_action(0, accept); /* 0 = the call that is ringing */
    else if (ble_phone_is_android()) gb_send(accept ? "{\"t\":\"call\",\"n\":\"ACCEPT\"}" : "{\"t\":\"call\",\"n\":\"REJECT\"}");
}

void ble_init(void)
{
    ble_uuid_from_str(&U_NUS, "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    ble_uuid_from_str(&U_NUS_RX, "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    ble_uuid_from_str(&U_NUS_TX, "6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    ble_uuid_from_str(&U_ANCS, "7905f431-b5ce-4e99-a40f-4b1e122d00d0");
    ble_uuid_from_str(&U_BULK, "4f524249-5400-4c4e-8b00-000000000001");
    ble_uuid_from_str(&U_BULK_RX, "4f524249-5400-4c4e-8b00-000000000002");
    ble_uuid_from_str(&U_BULK_TX, "4f524249-5400-4c4e-8b00-000000000003");

    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth did not start");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(SERVICES) != 0 || ble_gatts_add_svcs(SERVICES) != 0) {
        ESP_LOGE(TAG, "Bluetooth services rejected");
        return;
    }
    ble_svc_gap_device_name_set(NAME_PHONE);
    ble_store_config_init();

    const esp_timer_create_args_t t = {.callback = secure_cb, .name = "ble-secure"};
    esp_timer_create(&t, &secure_timer);
    hid_jobs = xQueueCreate(8, sizeof(hid_job_t));
    tx_queue = xQueueCreate(48, sizeof(tx_item_t));
    xTaskCreateWithCaps(tx_task, "ble-tx", 3072, NULL, 5, NULL, MALLOC_CAP_SPIRAM);
    xTaskCreateWithCaps(hid_task, "hid", 3072, NULL, 5, NULL, MALLOC_CAP_SPIRAM);
    nimble_port_freertos_init(host_task);
}
