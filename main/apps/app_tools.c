/* Small single-purpose apps: Find Phone, Sun times, Exposure, Workout and Presenter. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/ui.h"

#define RAD (3.14159265358979 / 180.0)

static lv_obj_t *big(lv_obj_t *scr, const char *text, int y)
{
    lv_obj_t *l = ui_label(scr, text, &font_num_big, C_TEXT);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

static lv_obj_t *caption(lv_obj_t *scr, const char *text, int y)
{
    lv_obj_t *l = ui_label(scr, text, F16, C_DIM);
    lv_obj_set_width(l, 320);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

/* ------------------------------------------------------------- find phone */

#define FIND_COLOR 0x4ADE9B
static lv_obj_t *fp_btn, *fp_text;
static bool fp_ringing;

static void find_paint(void)
{
    bool android = S.link.status == LINK_CONNECTED && S.link.peer == PEER_ANDROID;
    lv_obj_set_style_bg_color(fp_btn, fp_ringing ? lv_color_hex(FIND_COLOR) : C_SURF2, 0);
    ui_icon_set(ui_btn_icon(fp_btn), NULL, fp_ringing ? lv_color_black() : (android ? C_TEXT : C_FAINT));
    lv_label_set_text(fp_text, fp_ringing ? "Ringing. Tap to stop."
                                          : android ? "Tap to make your phone ring"
                                          : S.link.peer == PEER_IPHONE ? "iPhones don't offer this to accessories"
                                          : "Phone not connected");
}

static void find_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.link.status != LINK_CONNECTED || S.link.peer != PEER_ANDROID) return;
    fp_ringing = !fp_ringing;
    svc_find_phone(fp_ringing);
    find_paint();
}

static void find_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (fp_ringing) svc_find_phone(false);
    fp_ringing = false;
}

static void find_create(lv_obj_t *scr)
{
    fp_btn = ui_round_btn(scr, 170, C_SURF2, &ic_target_48, C_TEXT, find_tap, NULL);
    lv_obj_align(fp_btn, LV_ALIGN_CENTER, 0, -30);
    fp_text = caption(scr, "", 100);
    lv_obj_add_event_cb(scr, find_closed, LV_EVENT_DELETE, NULL);
    find_paint();
}

const app_t app_findphone = {
    .id = "find", .name = "Find Phone", .icon = &ic_target_48, .color = FIND_COLOR, .create = find_create,
};

/* -------------------------------------------------------------- sun times */

#define SUN_COLOR 0xFFB68C

/* Minutes after local midnight when the sun crosses `elevation` degrees,
 * rising or setting. Returns false if it never does that day (polar days).
 * Standard low-precision solar position formulas, good to a minute or two. */
static bool sun_event(double lat, double lon, int yday, double elevation, bool rising, int tz_minutes, int *minutes)
{
    double g = 2.0 * 3.14159265358979 / 365.0 * (yday + 0.5);
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g) -
                  0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
    double cos_ha = (sin(elevation * RAD) - sin(lat * RAD) * sin(decl)) / (cos(lat * RAD) * cos(decl));
    if (cos_ha < -1.0 || cos_ha > 1.0) return false;
    double ha = acos(cos_ha) / RAD;
    double utc = 720.0 - 4.0 * (lon + (rising ? ha : -ha)) - eqtime;
    int local = (int)lround(utc) + tz_minutes;
    *minutes = ((local % 1440) + 1440) % 1440;
    return true;
}

static void sun_row(lv_obj_t *scr, const char *name, bool ok, int minutes, int y, lv_color_t c)
{
    char buf[16];
    struct tm t = {.tm_hour = minutes / 60, .tm_min = minutes % 60};
    if (ok) fmt_clock(buf, sizeof(buf), &t, false);
    else snprintf(buf, sizeof(buf), "--");
    lv_obj_align(ui_label(scr, name, F20, C_DIM), LV_ALIGN_CENTER, -74, y);
    lv_obj_align(ui_label(scr, buf, F24, c), LV_ALIGN_CENTER, 92, y);
}

static void sun_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(SUN_COLOR);
    double place[2] = {0, 0};
    bool known = svc_kv_load("place", place, sizeof(place));
    if (S.gps.fix) place[0] = S.gps.lat, place[1] = S.gps.lon, known = true;
    lv_obj_align(ui_icon(scr, &ic_sunrise_48, c), LV_ALIGN_TOP_MID, 0, 48);
    if (!known) {
        lv_obj_align(ui_label(scr, "Where are you?", F28, C_TEXT), LV_ALIGN_CENTER, 0, -30);
        caption(scr, "Save a map area on the setup page, or open Maps with an Android phone connected.", 40);
        return;
    }
    if (S.gps.fix) svc_kv_save("place", place, sizeof(place));

    struct tm now;
    svc_localtime(&now);
    int up = 0, down = 0, gold_am = 0, gold_pm = 0, blue = 0;
    bool a = sun_event(place[0], place[1], now.tm_yday, -0.833, true, CFG.tz_minutes, &up);
    bool b = sun_event(place[0], place[1], now.tm_yday, 6.0, true, CFG.tz_minutes, &gold_am);
    bool d = sun_event(place[0], place[1], now.tm_yday, 6.0, false, CFG.tz_minutes, &gold_pm);
    bool f = sun_event(place[0], place[1], now.tm_yday, -0.833, false, CFG.tz_minutes, &down);
    bool g = sun_event(place[0], place[1], now.tm_yday, -6.0, false, CFG.tz_minutes, &blue);
    sun_row(scr, "Sunrise", a, up, -96, C_TEXT);
    sun_row(scr, "Golden ends", b, gold_am, -52, c);
    sun_row(scr, "Golden starts", d, gold_pm, -8, c);
    sun_row(scr, "Sunset", f, down, 36, C_TEXT);
    sun_row(scr, "Blue hour ends", g, blue, 80, lv_color_hex(0xA8C7FA));
    char where[40];
    snprintf(where, sizeof(where), "%.2f, %.2f", place[0], place[1]);
    caption(scr, where, 128);
}

const app_t app_sun = {
    .id = "sun", .name = "Sun Times", .icon = &ic_sunrise_48, .color = SUN_COLOR, .create = sun_create,
};

/* --------------------------------------------------------------- exposure */

#define EXPO_COLOR 0xFFC53D

/* Long exposures with a neutral-density filter: pick the metered shutter
 * speed and the filter strength, read off the new time, and time it. */
static const float SPEEDS[] = {1.0f / 4000, 1.0f / 2000, 1.0f / 1000, 1.0f / 500, 1.0f / 250, 1.0f / 125, 1.0f / 60,
                               1.0f / 30,   1.0f / 15,   1.0f / 8,    1.0f / 4,   0.5f,       1,          2,
                               4,           8,           15,          30};
static const uint8_t STOPS[] = {0, 3, 6, 10, 13, 16};
static int ex_speed = 6, ex_nd = 3;
static lv_obj_t *ex_base, *ex_filter, *ex_result, *ex_timer;
static float ex_seconds;

static void shutter_text(char *buf, size_t len, float s)
{
    if (s < 0.4f) snprintf(buf, len, "1/%d", (int)lroundf(1.0f / s));
    else if (s < 60) snprintf(buf, len, s < 10 && s != floorf(s) ? "%.1f s" : "%.0f s", (double)s);
    else snprintf(buf, len, "%d min %02d s", (int)s / 60, (int)s % 60);
}

static void expo_paint(void)
{
    char buf[24];
    shutter_text(buf, sizeof(buf), SPEEDS[ex_speed]);
    lv_label_set_text(ex_base, buf);
    snprintf(buf, sizeof(buf), STOPS[ex_nd] ? "ND %d stops" : "No filter", STOPS[ex_nd]);
    lv_label_set_text(ex_filter, buf);
    ex_seconds = SPEEDS[ex_speed] * (float)(1u << STOPS[ex_nd]);
    shutter_text(buf, sizeof(buf), ex_seconds);
    lv_label_set_text(ex_result, buf);
    if (ex_seconds >= 2 && ex_seconds <= 99 * 60) lv_obj_remove_flag(ex_timer, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ex_timer, LV_OBJ_FLAG_HIDDEN);
}

static void expo_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (d == 1 || d == -1) ex_speed = LV_CLAMP(0, ex_speed + d, (int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])) - 1);
    else ex_nd = LV_CLAMP(0, ex_nd + d / 10, (int)sizeof(STOPS) - 1);
    expo_paint();
}

static void expo_time(lv_event_t *e)
{
    LV_UNUSED(e);
    timer_start((int)lroundf(ex_seconds));
    shell_toast("Timer started");
}

static lv_obj_t *expo_row(lv_obj_t *scr, int y, int step)
{
    lv_obj_t *down = ui_round_btn(scr, 56, C_SURF2, &ic_minus_28, C_TEXT, expo_step, (void *)(intptr_t)-step);
    lv_obj_align(down, LV_ALIGN_CENTER, -138, y);
    lv_obj_t *up = ui_round_btn(scr, 56, C_SURF2, &ic_plus_28, C_TEXT, expo_step, (void *)(intptr_t)step);
    lv_obj_align(up, LV_ALIGN_CENTER, 138, y);
    lv_obj_t *l = ui_label(scr, "", F24, C_TEXT);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

static void expo_create(lv_obj_t *scr)
{
    caption(scr, "METERED SHUTTER", -132);
    ex_base = expo_row(scr, -98, 1);
    ex_filter = expo_row(scr, -30, 10);
    ex_result = ui_label(scr, "", F48, lv_color_hex(EXPO_COLOR));
    lv_obj_align(ex_result, LV_ALIGN_CENTER, 0, 50);
    ex_timer = ui_pill_btn(scr, "Time it", lv_color_hex(EXPO_COLOR), lv_color_black(), expo_time, NULL);
    lv_obj_align(ex_timer, LV_ALIGN_CENTER, 0, 132);
    expo_paint();
}

const app_t app_exposure = {
    .id = "exposure", .name = "Exposure", .icon = &ic_aperture_48, .color = EXPO_COLOR, .create = expo_create,
};

/* ---------------------------------------------------------------- workout */

#define WORK_COLOR 0xB1F0A0

static struct {
    bool active;
    uint32_t start_ms, start_steps;
} K;
static lv_obj_t *wk_time, *wk_stats, *wk_go;

static void workout_paint(lv_timer_t *t)
{
    LV_UNUSED(t);
    char buf[96];
    uint32_t s = K.active ? (svc_millis() - K.start_ms) / 1000 : 0;
    uint32_t steps = K.active && S.steps >= K.start_steps ? S.steps - K.start_steps : 0;
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    ui_label_set(wk_time, buf);
    unsigned metres = (unsigned)(steps * 3 / 4);
    snprintf(buf, sizeof(buf), "%u steps  " LV_SYMBOL_BULLET "  %u.%02u km  " LV_SYMBOL_BULLET "  %u kcal", (unsigned)steps,
             metres / 1000, metres % 1000 / 10, (unsigned)(steps / 25));
    ui_label_set(wk_stats, K.active ? buf : "Start a walk or run");
    lv_obj_set_style_bg_color(wk_go, K.active ? C_BAD : lv_color_hex(WORK_COLOR), 0);
    ui_icon_set(ui_btn_icon(wk_go), K.active ? &ic_stop_48 : &ic_play_48, lv_color_black());
}

static void workout_go(lv_event_t *e)
{
    LV_UNUSED(e);
    if (K.active) {
        char buf[64];
        uint32_t s = (svc_millis() - K.start_ms) / 1000, steps = S.steps >= K.start_steps ? S.steps - K.start_steps : 0;
        snprintf(buf, sizeof(buf), "%u min, %u steps, about %u kcal.", (unsigned)(s / 60), (unsigned)steps, (unsigned)(steps / 25));
        K.active = false;
        shell_alert("Workout done", buf);
    } else {
        K.active = true, K.start_ms = svc_millis(), K.start_steps = S.steps;
    }
    workout_paint(NULL);
}

static void workout_create(lv_obj_t *scr)
{
    lv_obj_align(ui_icon(scr, &ic_dumbbell_48, lv_color_hex(WORK_COLOR)), LV_ALIGN_TOP_MID, 0, 54);
    wk_time = big(scr, "", -52);
    wk_stats = caption(scr, "", 8);
    wk_go = ui_round_btn(scr, 104, lv_color_hex(WORK_COLOR), &ic_play_48, lv_color_black(), workout_go, NULL);
    lv_obj_align(wk_go, LV_ALIGN_CENTER, 0, 108);
    ui_timer_visual(scr, workout_paint, 1000, NULL);
    workout_paint(NULL);
}

const app_t app_workout = {
    .id = "workout", .name = "Workout", .icon = &ic_dumbbell_48, .color = WORK_COLOR, .create = workout_create,
    .keep_awake = true,
};

/* -------------------------------------------------------------- presenter */

#define SLIDE_COLOR 0x58B6FF
static lv_obj_t *pr_time, *pr_state;
static uint32_t pr_start;
static bool pr_keys_here;

static void presenter_paint(lv_timer_t *t)
{
    LV_UNUSED(t);
    char buf[16];
    uint32_t s = (svc_millis() - pr_start) / 1000;
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    ui_label_set(pr_time, buf);
    bool up = S.link.keys_mode && S.link.status == LINK_CONNECTED;
    ui_label_set(pr_state, up ? S.link.name : "Pair \"Orbit Keys\" first");
}

static void presenter_key(lv_event_t *e)
{
    svc_hid_keys(0, (uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void presenter_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (pr_keys_here) svc_link_keys_mode(false);
    pr_keys_here = false;
}

static void presenter_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(SLIDE_COLOR);
    pr_start = svc_millis();
    pr_state = ui_label_fit(scr, "", F14, C_DIM, 240); /* what the round screen allows this high up */
    lv_obj_align(pr_state, LV_ALIGN_TOP_MID, 0, 40);
    pr_time = ui_label(scr, "", F36, C_TEXT);
    lv_obj_align(pr_time, LV_ALIGN_TOP_MID, 0, 64);
    /* Arrow keys move slides in every presentation app; B blanks the screen. */
    lv_obj_t *next = ui_round_btn(scr, 170, c, &ic_next_48, lv_color_black(), presenter_key, (void *)0x4F);
    lv_obj_align(next, LV_ALIGN_CENTER, 62, 30);
    lv_obj_t *prev = ui_round_btn(scr, 96, C_SURF2, &ic_prev_48, C_TEXT, presenter_key, (void *)0x50);
    lv_obj_align(prev, LV_ALIGN_CENTER, -112, 30);
    lv_obj_t *blank = ui_pill_btn(scr, "Blank", C_SURF, C_DIM, presenter_key, (void *)0x05);
    lv_obj_set_height(blank, 44);
    lv_obj_align(blank, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_event_cb(scr, presenter_closed, LV_EVENT_DELETE, NULL);
    ui_timer_visual(scr, presenter_paint, 500, NULL);
    pr_keys_here = !S.link.keys_mode;
    if (pr_keys_here) svc_link_keys_mode(true);
    presenter_paint(NULL);
}

const app_t app_presenter = {
    .id = "presenter", .name = "Presenter", .icon = &ic_monitor_48, .color = SLIDE_COLOR, .create = presenter_create,
    .keep_awake = true, .no_clock = true,
};
