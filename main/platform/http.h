#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *url;
    const char *bearer;       /* optional "Authorization: Bearer ..." token */
    const char *content_type; /* for POST */
    const void *body;         /* non-NULL makes it a POST */
    size_t body_len;
    int timeout_ms;           /* 0 = 15 s */
} http_request_t;

typedef struct {
    char *data;   /* NUL-terminated for convenience; free with http_free() */
    size_t len;
    size_t cap;
    size_t max;   /* set before the call: largest reply accepted (default 64 KB) */
    bool overflow;
} http_reply_t;

/* Returns the HTTP status code, or -1 if the request failed or overflowed. */
int http_request(const http_request_t *q, http_reply_t *reply);
void http_free(http_reply_t *reply);
