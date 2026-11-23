/* LUMIX remote over the camera's own Wi-Fi.
 *
 * Uses the HTTP interface Panasonic's phone apps use ("cam.cgi"). The watch
 * joins the camera's network, introduces itself, switches the camera to
 * recording mode and then polls its state every couple of seconds, which
 * also keeps the session alive.
 *
 * Written from the publicly documented protocol; not yet run against a real
 * S5. If the camera answers differently, the log lines here show how.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "http.h"
#include "platform.h"

static const char *TAG = "lumix";

enum { JOB_LINK = 100, JOB_UNLINK };

static QueueHandle_t jobs;
static char base[40]; /* "http://<camera ip>" */
static bool linked;

/* ------------------------------------------------------------- UI updates */

static void cam_apply(void *arg)
{
    cam_t *c = arg;
    S.cam = *c;
    free(c);
    bus_emit(EVT_CAMERA, NULL);
}

static cam_t state = {.battery = -1, .shots_left = -1};

static void publish(cam_status_t status, const char *note)
{
    state.status = status;
    strlcpy(state.note, note ? note : "", sizeof(state.note));
    cam_t *c = malloc(sizeof(*c));
    if (!c) return;
    *c = state;
    ui_post(cam_apply, c);
}

/* ------------------------------------------------------------------- HTTP */

/* Sends one cam.cgi request. Returns true when the camera replied "ok". */
static bool cgi(const char *query, http_reply_t *keep)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/cam.cgi?%s", base, query);
    http_request_t q = {.url = url, .timeout_ms = 4000};
    http_reply_t r = {.max = 24 * 1024};
    int status = http_request(&q, &r);
    bool ok = status == 200 && r.data && strstr(r.data, "ok") != NULL;
    if (!ok) ESP_LOGW(TAG, "%s -> HTTP %d %.60s", query, status, r.data ? r.data : "");
    if (keep) *keep = r;
    else http_free(&r);
    return ok;
}

/* Copies the text inside <tag>...</tag>. */
static bool xml(const char *doc, const char *tag, char *out, size_t len)
{
    char open[32], close[32];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *a = doc ? strstr(doc, open) : NULL;
    const char *b = a ? strstr(a, close) : NULL;
    if (!a || !b) return false;
    a += strlen(open);
    size_t n = (size_t)(b - a);
    if (n >= len) n = len - 1;
    memcpy(out, a, n);
    out[n] = 0;
    return true;
}

static void poll_state(void)
{
    http_reply_t r;
    if (!cgi("mode=getstate", &r)) {
        http_free(&r);
        linked = false;
        publish(CAM_ERROR, "The camera stopped answering");
        return;
    }
    char v[24];
    cam_t before = state;
    /* Battery arrives as a fraction such as "2/3". */
    if (xml(r.data, "batt", v, sizeof(v))) {
        int num = 0, den = 0;
        if (sscanf(v, "%d/%d", &num, &den) == 2 && den > 0) state.battery = num * 100 / den;
    }
    if (xml(r.data, "remaincapacity", v, sizeof(v))) state.shots_left = atoi(v);
    if (xml(r.data, "rec", v, sizeof(v))) state.recording = strcmp(v, "on") == 0;
    http_free(&r);
    if (memcmp(&before, &state, sizeof(state)) != 0) publish(CAM_READY, NULL);
}

static void link_up(void)
{
    char ssid[33], pass[65];
    if (!net_secret_get("cam_ssid", ssid, sizeof(ssid))) {
        publish(CAM_ERROR, "Add the camera's Wi-Fi on the setup page");
        return;
    }
    net_secret_get("cam_pass", pass, sizeof(pass));
    publish(CAM_CONNECTING, "Joining the camera's Wi-Fi");
    if (!net_join_camera(ssid, pass)) {
        net_leave_camera();
        publish(CAM_ERROR, "Camera Wi-Fi not found. Is Remote Shooting on?");
        return;
    }

    /* The camera is the gateway of its own network. */
    esp_netif_ip_info_t ip = {0};
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) esp_netif_get_ip_info(sta, &ip);
    if (ip.gw.addr) snprintf(base, sizeof(base), "http://" IPSTR, IP2STR(&ip.gw));
    else strlcpy(base, "http://192.168.54.1", sizeof(base));

    /* Introduce ourselves. The camera may ask its owner to accept the first
     * time, so keep trying for a while. */
    publish(CAM_CONNECTING, "Accept the connection on the camera");
    bool ok = false;
    for (int i = 0; i < 20 && !ok; i++) {
        ok = cgi("mode=accctrl&type=req_acc&value=4D454930-0100-1000-8001-024F52424954&value2=Orbit", NULL);
        if (!ok) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!ok) {
        net_leave_camera();
        publish(CAM_ERROR, "The camera did not accept the watch");
        return;
    }
    cgi("mode=camcmd&value=recmode", NULL);

    http_reply_t r;
    if (cgi("mode=getinfo&type=capability", &r)) xml(r.data, "modelname", state.model, sizeof(state.model));
    http_free(&r);

    linked = true;
    publish(CAM_READY, NULL);
    poll_state();
}

static void link_down(void)
{
    linked = false;
    state = (cam_t){.battery = -1, .shots_left = -1};
    net_leave_camera();
    publish(CAM_OFF, NULL);
}

static void task(void *arg)
{
    LV_UNUSED(arg);
    for (;;) {
        int job;
        if (xQueueReceive(jobs, &job, pdMS_TO_TICKS(2000)) != pdTRUE) {
            if (linked) poll_state();
            continue;
        }
        if (job == JOB_LINK) {
            if (!linked) link_up();
        } else if (job == JOB_UNLINK) {
            if (linked || state.status != CAM_OFF) link_down();
        } else if (linked) {
            switch ((cam_cmd_t)job) {
            case CAMCMD_SHUTTER:
                cgi("mode=camcmd&value=capture", NULL);
                cgi("mode=camcmd&value=capture_cancel", NULL);
                break;
            case CAMCMD_RECORD_TOGGLE:
                cgi(state.recording ? "mode=camcmd&value=video_recstop" : "mode=camcmd&value=video_recstart", NULL);
                break;
            case CAMCMD_FOCUS: cgi("mode=camcmd&value=oneshot_af", NULL); break;
            }
            poll_state();
        }
    }
}

void svc_camera_link(bool on)
{
    /* The worker only exists once the Camera app has been used, so its stack
     * costs nothing on days the camera stays in the bag. */
    static bool started;
    if (on && jobs && !started) started = xTaskCreate(task, "lumix", 6144, NULL, 4, NULL) == pdPASS;
    int job = on ? JOB_LINK : JOB_UNLINK;
    if (jobs && started) xQueueSend(jobs, &job, 0);
}

void svc_camera(cam_cmd_t cmd)
{
    int job = cmd;
    if (jobs) xQueueSend(jobs, &job, 0);
}

void lumix_init(void)
{
    jobs = xQueueCreate(6, sizeof(int));
}
