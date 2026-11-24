/* Small helpers shared by the modules that read the phone's JSON lines. */
#pragma once

#include "cJSON.h"

/* The string under `key`, or "" when it is missing or not a string. */
static inline const char *json_str(cJSON *j, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

/* The number under `key`, or `fallback` when it is missing or not a number. */
static inline double json_num(cJSON *j, const char *key, double fallback)
{
    cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

/* Upper-cases ASCII letters in place. */
static inline void ascii_upper(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    }
}
