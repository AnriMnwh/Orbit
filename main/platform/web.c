/* The setup page served by the watch, and the small JSON API behind it.
 *
 * Reachable at http://orbit.local on the home network, or at 192.168.4.1 on
 * the setup hotspot. Secrets (Wi-Fi password, API key) can be written here
 * but are never sent back out.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "platform.h"

static const char *TAG = "web";

#define PHOTO_MAX_BYTES (400 * 1024)

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

/* ---------------------------------------------------------------- helpers */

static char *read_body(httpd_req_t *req, size_t max, size_t *len)
{
    size_t total = req->content_len;
    if (total == 0 || total > max) return NULL;
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, buf + got, total - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(buf);
            return NULL;
        }
        got += (size_t)n;
    }
    buf[total] = 0;
    if (len) *len = total;
    return buf;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text ? text : "{}");
    free(text);
    return err;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t fail(httpd_req_t *req, const char *why)
{
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, why);
}

static const char *str_of(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* Photo file names come from the browser, so only plain names are accepted. */
static bool safe_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 5 || n > 20 || strcasecmp(name + n - 4, ".jpg") != 0) return false;
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '.' && name[i] != '_' && name[i] != '-') return false;
    }
    return strstr(name, "..") == NULL;
}

/* ------------------------------------------------------------------ pages */

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

/* Anything unknown goes to the setup page, which is what makes the hotspot's
 * captive portal open by itself on phones. */
static esp_err_t h_fallback(httpd_req_t *req, httpd_err_code_t err)
{
    LV_UNUSED(err);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t h_status(httpd_req_t *req)
{
    char tmp[64];
    uint32_t total = 0, free_kb = 0;
    int done = 0, queued = 0;
    svc_storage_info(&total, &free_kb);
    tiles_progress(&done, &queued);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "version", ORBIT_VERSION);
    cJSON_AddBoolToObject(r, "wifi", S.wifi.connected);
    cJSON_AddStringToObject(r, "ssid", S.wifi.ssid);
    cJSON_AddStringToObject(r, "ip", S.wifi.ip);
    cJSON_AddBoolToObject(r, "setup", S.wifi.setup_mode);
    cJSON_AddNumberToObject(r, "battery", S.batt.percent);
    cJSON_AddBoolToObject(r, "charging", S.batt.charging);
    cJSON_AddNumberToObject(r, "photos", svc_photo_count());
    cJSON_AddNumberToObject(r, "storage_total_kb", total);
    cJSON_AddNumberToObject(r, "storage_free_kb", free_kb);
    cJSON_AddBoolToObject(r, "time_valid", svc_time_valid());
    cJSON_AddBoolToObject(r, "has_ai_key", net_secret_get("ai_key", tmp, sizeof(tmp)));
    cJSON_AddBoolToObject(r, "has_camera", net_secret_get("cam_ssid", tmp, sizeof(tmp)));
    cJSON_AddStringToObject(r, "camera_ssid", tmp);
    cJSON_AddNumberToObject(r, "tiles_done", done);
    cJSON_AddNumberToObject(r, "tiles_total", queued);
    cJSON_AddStringToObject(r, "phone", S.link.status == LINK_CONNECTED ? S.link.name : "");
    return send_json(req, r);
}

/* ------------------------------------------------------------------ Wi-Fi */

static esp_err_t h_scan(httpd_req_t *req)
{
    cJSON *list = cJSON_CreateArray();
    wifi_scan_config_t cfg = {.show_hidden = false};
    if (esp_wifi_scan_start(&cfg, true) == ESP_OK) {
        uint16_t n = 20;
        wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
        if (aps && esp_wifi_scan_get_ap_records(&n, aps) == ESP_OK) {
            for (int i = 0; i < n; i++) {
                if (!aps[i].ssid[0]) continue;
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "ssid", (const char *)aps[i].ssid);
                cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
                cJSON_AddBoolToObject(o, "secure", aps[i].authmode != WIFI_AUTH_OPEN);
                cJSON_AddItemToArray(list, o);
            }
        }
        free(aps);
    }
    return send_json(req, list);
}

static void rejoin_task(void *arg)
{
    LV_UNUSED(arg);
    vTaskDelay(pdMS_TO_TICKS(800)); /* let the HTTP reply go out first */
    net_rejoin();
    vTaskDelete(NULL);
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    char *body = read_body(req, 512, NULL);
    cJSON *j = body ? cJSON_Parse(body) : NULL;
    free(body);
    const char *ssid = j ? str_of(j, "ssid") : NULL;
    const char *pass = j ? str_of(j, "pass") : NULL;
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (pass && strlen(pass) > 64)) {
        cJSON_Delete(j);
        return fail(req, "ssid required");
    }
    net_secret_set("wifi_ssid", ssid);
    net_secret_set("wifi_pass", pass ? pass : "");
    cJSON_Delete(j);
    xTaskCreate(rejoin_task, "rejoin", 4096, NULL, 4, NULL);
    return send_ok(req);
}

/* The phone's browser knows the right time and time zone: take both. */
static esp_err_t h_time(httpd_req_t *req)
{
    char *body = read_body(req, 256, NULL);
    cJSON *j = body ? cJSON_Parse(body) : NULL;
    free(body);
    cJSON *epoch = j ? cJSON_GetObjectItem(j, "epoch") : NULL;
    cJSON *tz = j ? cJSON_GetObjectItem(j, "tz") : NULL;
    if (!cJSON_IsNumber(epoch) || !cJSON_IsNumber(tz)) {
        cJSON_Delete(j);
        return fail(req, "epoch and tz required");
    }
    clock_set((time_t)epoch->valuedouble, tz->valueint, true);
    cJSON_Delete(j);
    return send_ok(req);
}

/* ----------------------------------------------------------------- photos */

static esp_err_t h_photo_list(httpd_req_t *req)
{
    static char names[PHOTO_LIMIT][24];
    int n = photos_list(names, PHOTO_LIMIT);
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(list, cJSON_CreateString(names[i]));
    return send_json(req, list);
}

static esp_err_t h_photo_get(httpd_req_t *req)
{
    const char *name = strrchr(req->uri, '/');
    char clean[24];
    strlcpy(clean, name ? name + 1 : "", sizeof(clean));
    char *q = strchr(clean, '?');
    if (q) *q = 0;
    if (!safe_name(clean)) return httpd_resp_send_404(req);

    char path[64];
    snprintf(path, sizeof(path), PHOTO_DIR "/%s", clean);
    FILE *f = fopen(path, "rb");
    if (!f) return httpd_resp_send_404(req);
    httpd_resp_set_type(req, "image/jpeg");
    char *buf = malloc(4096);
    size_t n;
    while (buf && (n = fread(buf, 1, 4096, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) break;
    }
    free(buf);
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t h_photo_add(httpd_req_t *req)
{
    char path[64], name[24];
    if (req->content_len < 200 || req->content_len > PHOTO_MAX_BYTES) return fail(req, "photo too large");
    if (!photos_new_path(path, sizeof(path), name)) return fail(req, "photo library is full");
    FILE *f = fopen(path, "wb");
    if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "storage error");

    char *buf = malloc(4096);
    size_t left = req->content_len;
    bool ok = buf != NULL;
    bool first = true;
    while (ok && left > 0) {
        int got = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) ok = false;
        else {
            /* Must be a JPEG: starts with FF D8. */
            if (first && (got < 2 || (uint8_t)buf[0] != 0xFF || (uint8_t)buf[1] != 0xD8)) ok = false;
            first = false;
            if (ok && fwrite(buf, 1, (size_t)got, f) != (size_t)got) ok = false;
            left -= (size_t)got;
        }
    }
    free(buf);
    fclose(f);
    if (!ok) {
        unlink(path);
        return fail(req, "upload failed");
    }
    photos_changed();
    return send_ok(req);
}

static esp_err_t h_photo_delete(httpd_req_t *req)
{
    char query[64], name[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK || !safe_name(name)) {
        return fail(req, "bad name");
    }
    char path[64];
    snprintf(path, sizeof(path), PHOTO_DIR "/%s", name);
    unlink(path);
    photos_changed();
    return send_ok(req);
}

/* ----------------------------------------------------------------- macros */

static esp_err_t h_macro_get(httpd_req_t *req)
{
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < MACRO_MAX; i++) {
        const macro_t *m = &MACROS[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "label", m->label);
        cJSON_AddNumberToObject(o, "type", m->type);
        cJSON_AddNumberToObject(o, "mods", m->mods);
        cJSON_AddNumberToObject(o, "key", m->key);
        cJSON_AddNumberToObject(o, "usage", m->usage);
        cJSON_AddStringToObject(o, "text", m->text);
        cJSON_AddItemToArray(list, o);
    }
    return send_json(req, list);
}

static void macros_apply(void *arg)
{
    memcpy(MACROS, arg, sizeof(MACROS));
    free(arg);
    macros_changed();
}

static int num_of(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

static esp_err_t h_macro_set(httpd_req_t *req)
{
    char *body = read_body(req, 6000, NULL);
    cJSON *j = body ? cJSON_Parse(body) : NULL;
    free(body);
    if (!cJSON_IsArray(j)) {
        cJSON_Delete(j);
        return fail(req, "expected a list");
    }
    macro_t *next = calloc(MACRO_MAX, sizeof(macro_t));
    if (!next) {
        cJSON_Delete(j);
        return httpd_resp_send_500(req);
    }
    for (int i = 0; i < MACRO_MAX && i < cJSON_GetArraySize(j); i++) {
        cJSON *o = cJSON_GetArrayItem(j, i);
        const char *label = str_of(o, "label"), *text = str_of(o, "text");
        strlcpy(next[i].label, label ? label : "", sizeof(next[i].label));
        strlcpy(next[i].text, text ? text : "", sizeof(next[i].text));
        int type = num_of(o, "type");
        next[i].type = (uint8_t)(type >= MACRO_KEYS && type <= MACRO_MEDIA ? type : MACRO_KEYS);
        next[i].mods = (uint8_t)(num_of(o, "mods") & 0x0F);
        next[i].key = (uint8_t)num_of(o, "key");
        next[i].usage = (uint16_t)num_of(o, "usage");
    }
    cJSON_Delete(j);
    ui_post(macros_apply, next);
    return send_ok(req);
}

/* ---------------------------------------------------------------- secrets */

/* Write-only: values go into flash and are never served back. */
static esp_err_t h_secrets(httpd_req_t *req)
{
    static const char *const keys[] = {"ai_key", "ai_chat", "ai_stt", "ai_tts", "ai_voice", "cam_ssid", "cam_pass", "tile_url"};
    char *body = read_body(req, 1024, NULL);
    cJSON *j = body ? cJSON_Parse(body) : NULL;
    free(body);
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return fail(req, "expected an object");
    }
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *v = str_of(j, keys[i]);
        if (v && strlen(v) < 240) net_secret_set(keys[i], v);
    }
    cJSON_Delete(j);
    return send_ok(req);
}

static esp_err_t h_tiles(httpd_req_t *req)
{
    char *body = read_body(req, 256, NULL);
    cJSON *j = body ? cJSON_Parse(body) : NULL;
    free(body);
    cJSON *lat = j ? cJSON_GetObjectItem(j, "lat") : NULL;
    cJSON *lon = j ? cJSON_GetObjectItem(j, "lon") : NULL;
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || lat->valuedouble < -85 || lat->valuedouble > 85 ||
        lon->valuedouble < -180 || lon->valuedouble > 180) {
        cJSON_Delete(j);
        return fail(req, "lat and lon required");
    }
    int queued = tiles_preload(lat->valuedouble, lon->valuedouble);
    /* Also remember the spot, so Sun Times works without a phone's GPS. */
    double place[2] = {lat->valuedouble, lon->valuedouble};
    svc_kv_save("place", place, sizeof(place));
    cJSON_Delete(j);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "queued", queued);
    return send_json(req, r);
}

/* ----------------------------------------------------------------- update */

#define UPDATE_WINDOW_S 300
static uint32_t update_until; /* svc_millis() deadline, 0 = closed */

int svc_update_allow(void)
{
    update_until = svc_millis() + UPDATE_WINDOW_S * 1000;
    return UPDATE_WINDOW_S;
}

static void restart_task(void *arg)
{
    LV_UNUSED(arg);
    vTaskDelay(pdMS_TO_TICKS(1200)); /* let the reply reach the browser */
    esp_restart();
}

/* Receives a firmware image into the spare slot and boots into it. Only
 * accepted for a few minutes after "Allow update" is pressed on the watch, so
 * nobody else on the network can replace the firmware. */
static esp_err_t h_update(httpd_req_t *req)
{
    if (!update_until || (int32_t)(update_until - svc_millis()) < 0) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "On the watch: Settings, System, Allow update");
    }
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!slot || req->content_len < 100000 || req->content_len > slot->size) return fail(req, "not a firmware file");

    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) return httpd_resp_send_500(req);
    char *buf = malloc(4096);
    size_t left = req->content_len;
    bool ok = buf != NULL;
    while (ok && left > 0) {
        int got = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0 || esp_ota_write(ota, buf, (size_t)got) != ESP_OK) ok = false;
        else left -= (size_t)got;
    }
    free(buf);
    /* esp_ota_end() checks the image is complete and intact before we switch. */
    if (!ok) esp_ota_abort(ota);
    else ok = esp_ota_end(ota) == ESP_OK && esp_ota_set_boot_partition(slot) == ESP_OK;
    if (!ok) return fail(req, "the file was not a valid Orbit firmware");

    update_until = 0;
    ESP_LOGI(TAG, "update installed, restarting");
    xTaskCreate(restart_task, "restart", 2048, NULL, 4, NULL);
    return send_ok(req);
}

/* ------------------------------------------------------------------ start */

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.stack_size = 8192;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "setup page did not start");
        return;
    }
    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_index},
        {.uri = "/api/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = h_scan},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = h_wifi},
        {.uri = "/api/time", .method = HTTP_POST, .handler = h_time},
        {.uri = "/api/photos", .method = HTTP_GET, .handler = h_photo_list},
        {.uri = "/api/photos", .method = HTTP_POST, .handler = h_photo_add},
        {.uri = "/api/photos", .method = HTTP_DELETE, .handler = h_photo_delete},
        {.uri = "/photos/*", .method = HTTP_GET, .handler = h_photo_get},
        {.uri = "/api/macros", .method = HTTP_GET, .handler = h_macro_get},
        {.uri = "/api/macros", .method = HTTP_POST, .handler = h_macro_set},
        {.uri = "/api/secrets", .method = HTTP_POST, .handler = h_secrets},
        {.uri = "/api/tiles", .method = HTTP_POST, .handler = h_tiles},
        {.uri = "/api/update", .method = HTTP_POST, .handler = h_update},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(server, &routes[i]);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, h_fallback);
}
