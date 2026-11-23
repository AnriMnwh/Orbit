/* Wi-Fi: home network, the setup hotspot, and hopping onto the camera's own
 * network. Also keeps the clock right (NTP) and answers to "orbit.local". */
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"

#include "platform.h"

static const char *TAG = "net";

#define GOT_IP BIT0
#define SETUP_SSID "Orbit-Setup"

static EventGroupHandle_t events;
static esp_timer_handle_t retry_timer;
static bool wifi_up, want_link, online, setup_mode, camera_mode, sntp_started;
static bool joining, failed; /* a network picked on the watch is being tried / was refused */
static int join_tries;
static char link_ssid[33], link_ip[16], setup_pass[12];
static TaskHandle_t dns_task_handle;

/* ---------------------------------------------------------------- secrets */

bool net_secret_get(const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    out[0] = 0;
    if (nvs_open("secrets", NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (err != ESP_OK) out[0] = 0;
    return err == ESP_OK && out[0];
}

void net_secret_set(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open("secrets", NVS_READWRITE, &h) != ESP_OK) return;
    if (value && value[0]) nvs_set_str(h, key, value);
    else nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------------ UI updates */

static void wifi_apply(void *arg)
{
    wifi_t *w = arg;
    S.wifi = *w;
    free(w);
    bus_emit(EVT_WIFI, NULL);
}

static void publish(void)
{
    wifi_t *w = calloc(1, sizeof(*w));
    if (!w) return;
    w->connected = online && !camera_mode;
    w->setup_mode = setup_mode;
    strlcpy(w->ssid, link_ssid, sizeof(w->ssid));
    strlcpy(w->ip, link_ip, sizeof(w->ip));
    strlcpy(w->setup_pass, setup_pass, sizeof(w->setup_pass));
    w->joining = joining;
    w->failed = failed;
    ui_post(wifi_apply, w);
}

bool net_online(void)
{
    return online && !camera_mode;
}

/* ------------------------------------------------------------------ clock */

static void on_time_sync(struct timeval *tv)
{
    clock_set(tv->tv_sec, 0, false);
}

static void sntp_begin(void)
{
    if (sntp_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_time_sync;
    sntp_started = esp_netif_sntp_init(&cfg) == ESP_OK;
}

/* ----------------------------------------------------------------- events */

static void retry_cb(void *arg)
{
    LV_UNUSED(arg);
    if (want_link && !online) esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LV_UNUSED(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (want_link) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        online = false;
        link_ip[0] = 0;
        xEventGroupClearBits(events, GOT_IP);
        /* A freshly typed password gets a few tries, then we say so instead
         * of retrying a wrong password for ever. */
        if (joining && ++join_tries >= 4) {
            joining = false, failed = true, want_link = false;
            net_secret_set("wifi_ssid", ""); /* it never worked, so do not keep it */
        }
        publish();
        if (want_link) {
            esp_timer_stop(retry_timer);
            esp_timer_start_once(retry_timer, 6 * 1000000ULL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(link_ip, sizeof(link_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        online = true;
        joining = false, failed = false;
        xEventGroupSetBits(events, GOT_IP);
        if (!camera_mode) sntp_begin();
        publish();
        ESP_LOGI(TAG, "joined %s as %s", link_ssid, link_ip);
    }
}

/* ------------------------------------------------------------------- core */

static void set_station(const char *ssid, const char *password)
{
    wifi_config_t c = {0};
    strlcpy((char *)c.sta.ssid, ssid, sizeof(c.sta.ssid));
    strlcpy((char *)c.sta.password, password ? password : "", sizeof(c.sta.password));
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    strlcpy(link_ssid, ssid, sizeof(link_ssid));
}

static void apply_mode(void)
{
    wifi_mode_t mode = setup_mode ? WIFI_MODE_APSTA : (want_link ? WIFI_MODE_STA : WIFI_MODE_NULL);
    if (mode == WIFI_MODE_NULL) {
        if (wifi_up) esp_wifi_stop();
        wifi_up = false;
        return;
    }
    esp_wifi_set_mode(mode);
    if (!wifi_up) {
        esp_wifi_start();
        wifi_up = true;
    } else if (want_link && !online) {
        esp_wifi_connect();
    }
}

/* Join the home network saved by the setup page (or stay off if none). */
static void join_home(void)
{
    char ssid[33], pass[65];
    camera_mode = false;
    want_link = net_secret_get("wifi_ssid", ssid, sizeof(ssid));
    if (want_link) {
        net_secret_get("wifi_pass", pass, sizeof(pass));
        set_station(ssid, pass);
    } else {
        link_ssid[0] = 0;
    }
    apply_mode();
    publish();
}

/* Called by the setup page after new credentials are saved. */
void net_rejoin(void)
{
    if (wifi_up) esp_wifi_disconnect();
    online = false;
    join_home();
}

bool net_join_camera(const char *ssid, const char *password)
{
    camera_mode = true;
    want_link = true;
    if (wifi_up) esp_wifi_disconnect();
    online = false;
    xEventGroupClearBits(events, GOT_IP);
    esp_wifi_set_mode(setup_mode ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    set_station(ssid, password);
    if (!wifi_up) {
        esp_wifi_start();
        wifi_up = true;
    } else {
        esp_wifi_connect();
    }
    publish();
    return xEventGroupWaitBits(events, GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000)) & GOT_IP;
}

void net_leave_camera(void)
{
    if (!camera_mode) return;
    if (wifi_up) esp_wifi_disconnect();
    online = false;
    join_home();
}

/* ---------------------------------------------------------- setup hotspot */

/* While the hotspot is up, every DNS lookup is answered with our own address,
 * which makes phones pop the setup page open on their own. */
static void dns_task(void *arg)
{
    LV_UNUSED(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (sock >= 0) close(sock);
        dns_task_handle = NULL;
        vTaskDelete(NULL);
    }
    struct timeval tv = {.tv_sec = 1};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    static uint8_t pkt[300];
    while (setup_mode) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, pkt, sizeof(pkt) - 16, 0, (struct sockaddr *)&from, &flen);
        if (n < 17) continue;
        /* The answer must follow the question directly, so find where the
         * question ends and drop anything the client added after it. */
        int q = 12;
        while (q < n && pkt[q]) q += pkt[q] + 1;
        q += 5; /* name terminator, type, class */
        if (q > n) continue;
        pkt[2] = 0x81, pkt[3] = 0x80;            /* response, recursion available */
        pkt[4] = 0, pkt[5] = 1;                  /* one question */
        pkt[6] = 0, pkt[7] = 1;                  /* one answer */
        pkt[8] = pkt[9] = pkt[10] = pkt[11] = 0; /* no authority / additional */
        static const uint8_t answer[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1};
        memcpy(pkt + q, answer, sizeof(answer));
        sendto(sock, pkt, (size_t)q + sizeof(answer), 0, (struct sockaddr *)&from, flen);
    }
    close(sock);
    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

static void setup_begin(void *arg)
{
    bool on = (uintptr_t)arg;
    if (on == setup_mode) return;
    setup_mode = on;
    if (on) {
        snprintf(setup_pass, sizeof(setup_pass), "%08u", (unsigned)(esp_random() % 100000000u));
        wifi_config_t ap = {.ap = {.ssid = SETUP_SSID, .ssid_len = strlen(SETUP_SSID), .channel = 1,
                                   .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK}};
        strlcpy((char *)ap.ap.password, setup_pass, sizeof(ap.ap.password));
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (!wifi_up) {
            esp_wifi_start();
            wifi_up = true;
        }
        if (!dns_task_handle) xTaskCreate(dns_task, "dns", 3072, NULL, 4, &dns_task_handle);
    } else {
        apply_mode();
    }
    publish();
}

/* Wi-Fi calls can block for a moment, so they run off the LVGL thread. */
static void worker(void *arg)
{
    setup_begin(arg);
    vTaskDelete(NULL);
}

void svc_wifi_setup_mode(bool on)
{
    xTaskCreate(worker, "wifi-setup", 4096, (void *)(uintptr_t)on, 4, NULL);
}

/* ------------------------------------------- choosing a network on the watch */

typedef struct {
    int count;
    wifi_net_t nets[WIFI_SCAN_MAX];
} scan_result_t;

static void scan_apply(void *arg)
{
    scan_result_t *r = arg;
    memcpy(S.scan, r->nets, sizeof(S.scan));
    S.scan_count = r->count;
    S.scanning = false;
    free(r);
    bus_emit(EVT_WIFI, NULL);
}

static void scan_task(void *arg)
{
    LV_UNUSED(arg);
    scan_result_t *r = calloc(1, sizeof(*r));
    /* Scanning needs the radio in station mode, even with no network saved. */
    if (!wifi_up) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        wifi_up = esp_wifi_start() == ESP_OK;
    }
    wifi_scan_config_t cfg = {.show_hidden = false};
    uint16_t n = 24;
    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    if (r && aps && esp_wifi_scan_start(&cfg, true) == ESP_OK && esp_wifi_scan_get_ap_records(&n, aps) == ESP_OK) {
        /* Results come strongest first; keep the first sighting of each name. */
        for (int i = 0; i < n && r->count < WIFI_SCAN_MAX; i++) {
            bool seen = !aps[i].ssid[0];
            for (int k = 0; k < r->count && !seen; k++) seen = strcmp(r->nets[k].ssid, (const char *)aps[i].ssid) == 0;
            if (seen) continue;
            wifi_net_t *net = &r->nets[r->count++];
            strlcpy(net->ssid, (const char *)aps[i].ssid, sizeof(net->ssid));
            net->rssi = aps[i].rssi;
            net->secure = aps[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    free(aps);
    if (r) ui_post(scan_apply, r);
    vTaskDelete(NULL);
}

void svc_wifi_scan(void)
{
    if (S.scanning) return;
    S.scanning = true;
    S.scan_count = 0;
    bus_emit(EVT_WIFI, NULL);
    if (xTaskCreate(scan_task, "wifi-scan", 4096, NULL, 4, NULL) != pdPASS) S.scanning = false;
}

typedef struct {
    char ssid[33], pass[65];
} join_t;

static void join_task(void *arg)
{
    join_t *j = arg;
    net_secret_set("wifi_ssid", j->ssid);
    net_secret_set("wifi_pass", j->pass);
    joining = j->ssid[0] != 0, failed = false, join_tries = 0;
    free(j);
    net_rejoin();
    vTaskDelete(NULL);
}

void svc_wifi_join(const char *ssid, const char *password)
{
    join_t *j = calloc(1, sizeof(*j));
    if (!j) return;
    strlcpy(j->ssid, ssid, sizeof(j->ssid));
    strlcpy(j->pass, password ? password : "", sizeof(j->pass));
    if (xTaskCreate(join_task, "wifi-join", 4096, j, 4, NULL) != pdPASS) free(j);
}

void svc_wifi_forget(void)
{
    svc_wifi_join("", ""); /* saving an empty name erases it; rejoining then switches Wi-Fi off */
}

bool svc_configured(int what)
{
    static const char *const keys[] = {[HAS_WIFI] = "wifi_ssid", [HAS_AI_KEY] = "ai_key", [HAS_CAMERA] = "cam_ssid"};
    char value[200];
    return what >= 0 && what <= HAS_CAMERA && net_secret_get(keys[what], value, sizeof(value));
}

void net_init(void)
{
    events = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi did not start");
        return;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    const esp_timer_create_args_t t = {.callback = retry_cb, .name = "wifi-retry"};
    esp_timer_create(&t, &retry_timer);

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("orbit");
        mdns_instance_name_set("Orbit watch");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    join_home();
}
