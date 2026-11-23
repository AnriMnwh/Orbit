/* One blocking HTTP(S) request with the whole reply collected in memory.
 * Shared by map tiles, the camera remote and the assistant. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "http.h"

static esp_err_t on_data(esp_http_client_event_t *e)
{
    http_reply_t *r = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || !r || e->data_len <= 0) return ESP_OK;
    size_t need = r->len + (size_t)e->data_len + 1;
    if (need > r->max) {
        r->overflow = true;
        return ESP_OK;
    }
    if (need > r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 4096;
        while (cap < need) cap *= 2;
        if (cap > r->max) cap = r->max;
        char *grown = heap_caps_realloc(r->data, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!grown) {
            r->overflow = true;
            return ESP_OK;
        }
        r->data = grown, r->cap = cap;
    }
    memcpy(r->data + r->len, e->data, (size_t)e->data_len);
    r->len += (size_t)e->data_len;
    r->data[r->len] = 0;
    return ESP_OK;
}

int http_request(const http_request_t *q, http_reply_t *reply)
{
    reply->data = NULL, reply->len = 0, reply->cap = 0, reply->overflow = false;
    if (!reply->max) reply->max = 64 * 1024;

    esp_http_client_config_t cfg = {
        .url = q->url,
        .method = q->body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = q->timeout_ms ? q->timeout_ms : 15000,
        .event_handler = on_data,
        .user_data = reply,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .user_agent = "Orbit-watch/0.1",
        .keep_alive_enable = false,
    };
    if (strncmp(q->url, "https://", 8) == 0) cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    if (q->bearer) {
        size_t n = strlen(q->bearer) + 8;
        char *h = malloc(n);
        if (h) {
            snprintf(h, n, "Bearer %s", q->bearer);
            esp_http_client_set_header(c, "Authorization", h);
            free(h);
        }
    }
    if (q->content_type) esp_http_client_set_header(c, "Content-Type", q->content_type);
    if (q->body) esp_http_client_set_post_field(c, q->body, (int)q->body_len);

    int status = -1;
    if (esp_http_client_perform(c) == ESP_OK) status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (reply->overflow) status = -1;
    return status;
}

void http_free(http_reply_t *reply)
{
    free(reply->data);
    reply->data = NULL, reply->len = 0, reply->cap = 0;
}
