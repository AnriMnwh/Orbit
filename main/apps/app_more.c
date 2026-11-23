/* Alarm, Weather, Fitness, Relax and Calculator. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/ui.h"

/* ------------------------------------------------------------------ alarm */

#define ALARM_COLOR 0xFDD663

static struct {
    uint8_t hour, minute, on;
    uint8_t fired; /* guards against ringing twice within the same minute */
} A = {.hour = 7, .minute = 0};

static lv_obj_t *al_scr, *al_time, *al_toggle, *al_toggle_label;

static void alarm_paint(void)
{
    if (!al_scr) return;
    char buf[16];
    struct tm t = {.tm_hour = A.hour, .tm_min = A.minute};
    fmt_clock(buf, sizeof(buf), &t, false);
    lv_label_set_text(al_time, buf);
    lv_obj_set_style_text_color(al_time, A.on ? C_TEXT : C_FAINT, 0);
    lv_obj_set_style_bg_color(al_toggle, A.on ? lv_color_hex(ALARM_COLOR) : C_SURF2, 0);
    lv_obj_set_style_text_color(al_toggle_label, A.on ? lv_color_black() : C_TEXT, 0);
    snprintf(buf, sizeof(buf), A.on ? "On  %s" : "Off", CFG.clock_24h ? "" : fmt_ampm(&t));
    lv_label_set_text(al_toggle_label, buf);
}

static void alarm_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e); /* +-1 hour, +-100 = +-5 minutes */
    if (d == 1 || d == -1) A.hour = (uint8_t)((A.hour + 24 + d) % 24);
    else A.minute = (uint8_t)((A.minute + 60 + (d > 0 ? 5 : -5)) % 60);
    svc_kv_save("alarm", &A, sizeof(A));
    alarm_paint();
}

static void alarm_toggle(lv_event_t *e)
{
    LV_UNUSED(e);
    A.on = !A.on;
    svc_kv_save("alarm", &A, sizeof(A));
    alarm_paint();
}

/* For the alarm tile. */
void alarm_get(char *text, size_t len, bool *on)
{
    struct tm t = {.tm_hour = A.hour, .tm_min = A.minute};
    fmt_clock(text, len, &t, false);
    *on = A.on;
}

void alarm_set_on(bool on)
{
    A.on = on;
    svc_kv_save("alarm", &A, sizeof(A));
    alarm_paint();
}

static void alarm_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    al_scr = NULL;
}

static void alarm_create(lv_obj_t *scr)
{
    al_scr = scr;
    lv_obj_add_event_cb(scr, alarm_closed, LV_EVENT_DELETE, NULL);
    lv_obj_align(ui_label(scr, "ALARM", F16, C_DIM), LV_ALIGN_TOP_MID, 0, 56);
    al_time = ui_label(scr, "", &font_num_big, C_TEXT);
    lv_obj_align(al_time, LV_ALIGN_CENTER, 0, -62);

    static const struct {
        const char *label;
        int step, x;
    } keys[] = {{"-", -1, -132}, {"+", 1, -62}, {"-", -100, 62}, {"+", 100, 132}};
    for (size_t i = 0; i < 4; i++) {
        lv_obj_t *b = ui_round_btn(scr, 60, C_SURF2, keys[i].step > 0 ? &ic_plus_28 : &ic_minus_28, C_TEXT, alarm_step,
                                   (void *)(intptr_t)keys[i].step);
        lv_obj_align(b, LV_ALIGN_CENTER, keys[i].x, 30);
    }
    lv_obj_align(ui_label(scr, "hour", F14, C_FAINT), LV_ALIGN_CENTER, -97, 76);
    lv_obj_align(ui_label(scr, "minute", F14, C_FAINT), LV_ALIGN_CENTER, 97, 76);

    al_toggle = ui_pill_btn(scr, "Off", C_SURF2, C_TEXT, alarm_toggle, NULL);
    al_toggle_label = lv_obj_get_child(al_toggle, 0);
    lv_obj_align(al_toggle, LV_ALIGN_CENTER, 0, 138);
    alarm_paint();
}

const app_t app_alarm = {
    .id = "alarm", .name = "Alarm", .icon = &ic_alarm_48, .color = ALARM_COLOR, .create = alarm_create,
};

/* --------------------------------------------------------------- fitness */

#define FIT_COLOR 0xA8DAB5

/* Steps for the last seven days; today is index 6. `day` is the day of the
 * year the last slot belongs to, so a new day can shift everything along. */
static struct {
    int16_t day;
    uint16_t steps[7];
} F = {.day = -1};

static lv_obj_t *fit_scr, *fit_arc, *fit_steps, *fit_stats, *fit_bars[7];

static void fitness_paint(void)
{
    if (!fit_scr) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "%u", (unsigned)S.steps);
    lv_label_set_text(fit_steps, buf);
    ui_arc_set(fit_arc, (int)(S.steps * 100 / CFG.step_goal));
    /* Rough estimates: 0.75 m and 0.04 kcal a step. */
    unsigned metres = (unsigned)(S.steps * 3 / 4), kcal = (unsigned)(S.steps / 25);
    snprintf(buf, sizeof(buf), "%u.%u km   " LV_SYMBOL_BULLET "   %u kcal", metres / 1000, metres % 1000 / 100, kcal);
    lv_label_set_text(fit_stats, buf);
    for (int i = 0; i < 7; i++) {
        unsigned v = i == 6 ? S.steps : F.steps[i];
        int h = 6 + (int)(v > CFG.step_goal ? 54 : v * 54 / CFG.step_goal);
        lv_obj_set_height(fit_bars[i], h);
        lv_obj_set_style_bg_color(fit_bars[i], i == 6 ? lv_color_hex(FIT_COLOR) : C_SURF2, 0);
    }
}

static void fitness_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    fit_scr = NULL;
}

static void fitness_create(lv_obj_t *scr)
{
    fit_scr = scr;
    lv_obj_add_event_cb(scr, fitness_closed, LV_EVENT_DELETE, NULL);
    fit_arc = ui_arc(scr, 436, 14, C_SURF, lv_color_hex(FIT_COLOR), 135, 270);
    lv_obj_center(fit_arc);
    lv_obj_align(ui_label(scr, "STEPS TODAY", F16, C_DIM), LV_ALIGN_CENTER, 0, -118);
    fit_steps = ui_label(scr, "", &font_num_big, C_TEXT);
    lv_obj_align(fit_steps, LV_ALIGN_CENTER, 0, -62);
    fit_stats = ui_label(scr, "", F20, C_DIM);
    lv_obj_align(fit_stats, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *chart = ui_box(scr, 7 * 26, 62);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 68);
    for (int i = 0; i < 7; i++) {
        fit_bars[i] = ui_box(chart, 16, 6);
        lv_obj_set_style_radius(fit_bars[i], 8, 0);
        lv_obj_set_style_bg_opa(fit_bars[i], LV_OPA_COVER, 0);
        lv_obj_align(fit_bars[i], LV_ALIGN_BOTTOM_LEFT, 5 + i * 26, 0);
    }
    lv_obj_align(ui_label(scr, "last 7 days", F14, C_FAINT), LV_ALIGN_CENTER, 0, 116);
    fitness_paint();
}

const app_t app_fitness = {
    .id = "fitness", .name = "Fitness", .icon = &ic_steps_48, .color = FIT_COLOR, .create = fitness_create,
};

/* ---------------------------------------------------------------- weather */

#define WEATHER_COLOR 0xA8C7FA

static const lv_image_dsc_t *weather_icon(void)
{
    const char *t = S.weather.text;
    bool clear = strstr(t, "lear") || strstr(t, "unny") || strstr(t, "Sun");
    return clear ? &ic_sun_48 : &ic_cloud_48;
}

static void weather_fill(lv_obj_t *parent, bool tile)
{
    lv_obj_clean(parent);
    if (!S.weather.valid) {
        lv_obj_align(ui_icon(parent, &ic_cloud_48, C_FAINT), LV_ALIGN_CENTER, 0, -70);
        lv_obj_align(ui_label(parent, "No weather yet", F24, C_TEXT), LV_ALIGN_CENTER, 0, -10);
        lv_obj_t *h = ui_label(parent, "Android phones send it through Gadgetbridge.", F16, C_DIM);
        lv_obj_set_width(h, 290);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(h, LV_ALIGN_CENTER, 0, 44);
        return;
    }
    char buf[32];
    lv_obj_align(ui_icon(parent, weather_icon(), lv_color_hex(WEATHER_COLOR)), LV_ALIGN_CENTER, 0, -112);
    snprintf(buf, sizeof(buf), "%d", S.weather.temp_c);
    lv_obj_t *temp = ui_label(parent, buf, &font_num_big, C_TEXT);
    lv_obj_align(temp, LV_ALIGN_CENTER, 0, -34);
    lv_obj_align_to(ui_label(parent, "\xC2\xB0", F48, C_TEXT), temp, LV_ALIGN_OUT_RIGHT_TOP, 2, 8);
    lv_obj_align(ui_label_fit(parent, S.weather.text, F24, C_TEXT, 300), LV_ALIGN_CENTER, 0, 36);
    snprintf(buf, sizeof(buf), "High %d\xC2\xB0   Low %d\xC2\xB0", S.weather.hi_c, S.weather.lo_c);
    lv_obj_align(ui_label(parent, buf, F20, C_DIM), LV_ALIGN_CENTER, 0, 76);
    LV_UNUSED(tile);
}

static void weather_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    if (e == EVT_WEATHER) weather_fill(user, false);
}

static void weather_create(lv_obj_t *scr)
{
    lv_obj_t *box = ui_box(scr, SCR, SCR);
    bus_watch(scr, weather_event, box);
    weather_fill(box, false);
}

const app_t app_weather = {
    .id = "weather", .name = "Weather", .icon = &ic_cloud_48, .color = WEATHER_COLOR, .create = weather_create,
};

static void weather_tile_open(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(&app_weather);
}

void weather_tile_create(lv_obj_t *tile)
{
    lv_obj_t *box = ui_box(tile, SCR, SCR);
    lv_obj_add_event_cb(box, weather_tile_open, LV_EVENT_SHORT_CLICKED, NULL);
    bus_watch(tile, weather_event, box);
    weather_fill(box, true);
}

/* ------------------------------------------------------------------ relax */

#define RELAX_COLOR 0x7CE0D6
#define BREATH_MS 4000

static lv_obj_t *rx_ring, *rx_text, *rx_count, *rx_go;
static uint32_t rx_started;
static bool rx_running;
static int rx_minutes = 1;

static void relax_frame(lv_timer_t *t)
{
    LV_UNUSED(t);
    char buf[24];
    if (!rx_running) {
        lv_obj_set_size(rx_ring, 150, 150);
        lv_label_set_text(rx_text, "Ready");
        snprintf(buf, sizeof(buf), "%d min", rx_minutes);
        lv_label_set_text(rx_count, buf);
        return;
    }
    uint32_t el = svc_millis() - rx_started;
    uint32_t total = (uint32_t)rx_minutes * 60000;
    if (el >= total) {
        rx_running = false;
        lv_label_set_text(lv_obj_get_child(rx_go, 0), "Start");
        if (CFG.sound) svc_sound(SND_OK);
        shell_toast("Nicely done");
        return;
    }
    /* In for four seconds, out for four, eased so it never jerks. */
    uint32_t phase = el % (2 * BREATH_MS);
    bool in = phase < BREATH_MS;
    float p = (float)(in ? phase : 2 * BREATH_MS - phase) / BREATH_MS;
    float eased = 0.5f - 0.5f * cosf(p * 3.14159265f);
    int size = 110 + (int)(150 * eased);
    lv_obj_set_size(rx_ring, size, size);
    lv_label_set_text(rx_text, in ? "Breathe in" : "Breathe out");
    uint32_t left = (total - el + 999) / 1000;
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
    lv_label_set_text(rx_count, buf);
}

static void relax_go(lv_event_t *e)
{
    LV_UNUSED(e);
    rx_running = !rx_running;
    rx_started = svc_millis();
    lv_label_set_text(lv_obj_get_child(rx_go, 0), rx_running ? "Stop" : "Start");
}

static void relax_length(lv_event_t *e)
{
    LV_UNUSED(e);
    if (rx_running) return;
    rx_minutes = rx_minutes == 1 ? 2 : (rx_minutes == 2 ? 5 : 1);
}

static void relax_create(lv_obj_t *scr)
{
    rx_running = false;
    lv_color_t c = lv_color_hex(RELAX_COLOR);
    rx_ring = ui_box(scr, 150, 150);
    lv_obj_set_style_radius(rx_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(rx_ring, ui_tint(c, 34), 0);
    lv_obj_set_style_bg_opa(rx_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(rx_ring, c, 0);
    lv_obj_set_style_border_width(rx_ring, 4, 0);
    lv_obj_align(rx_ring, LV_ALIGN_CENTER, 0, -34);
    lv_obj_add_flag(rx_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rx_ring, relax_length, LV_EVENT_CLICKED, NULL);

    rx_text = ui_label(scr, "", F24, C_TEXT);
    lv_obj_align(rx_text, LV_ALIGN_CENTER, 0, -44);
    rx_count = ui_label(scr, "", F16, C_DIM);
    lv_obj_align(rx_count, LV_ALIGN_CENTER, 0, -12);

    rx_go = ui_pill_btn(scr, "Start", c, lv_color_black(), relax_go, NULL);
    lv_obj_align(rx_go, LV_ALIGN_CENTER, 0, 150);
    ui_timer(scr, relax_frame, 50, NULL);
    relax_frame(NULL);
}

const app_t app_relax = {
    .id = "relax", .name = "Relax", .icon = &ic_wind_48, .color = RELAX_COLOR, .create = relax_create, .keep_awake = true,
};

/* ------------------------------------------------------------- calculator */

#define CALC_COLOR 0xD0BCFF

static lv_obj_t *ca_display;
static char ca_entry[20];
static double ca_acc;
static char ca_op;
static bool ca_fresh; /* the next digit starts a new number */

static void calc_show(double v)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%.9g", v);
    lv_label_set_text(ca_display, buf);
}

static void calc_apply(void)
{
    double v = strtod(ca_entry[0] ? ca_entry : "0", NULL);
    switch (ca_op) {
    case '+': ca_acc += v; break;
    case '-': ca_acc -= v; break;
    case 'x': ca_acc *= v; break;
    case '/': ca_acc = v != 0 ? ca_acc / v : 0; break;
    default: ca_acc = v; break;
    }
}

static void calc_key(lv_event_t *e)
{
    char k = (char)(intptr_t)lv_event_get_user_data(e);
    size_t n = strlen(ca_entry);
    if ((k >= '0' && k <= '9') || k == '.') {
        if (ca_fresh) ca_entry[0] = 0, n = 0, ca_fresh = false;
        if (k == '.' && strchr(ca_entry, '.')) return;
        if (n < 11) ca_entry[n] = k, ca_entry[n + 1] = 0; /* as many digits as the display fits */
        lv_label_set_text(ca_display, ca_entry);
    } else if (k == 'C') {
        ca_entry[0] = 0, ca_acc = 0, ca_op = 0, ca_fresh = false;
        lv_label_set_text(ca_display, "0");
    } else {
        /* An operator or equals: fold the number just typed into the total. */
        if (!ca_fresh || k == '=') calc_apply();
        calc_show(ca_acc);
        snprintf(ca_entry, sizeof(ca_entry), "%.9g", ca_acc);
        ca_op = k == '=' ? 0 : k;
        ca_fresh = true;
    }
}

static void calc_create(lv_obj_t *scr)
{
    ca_entry[0] = 0, ca_acc = 0, ca_op = 0, ca_fresh = false;
    ca_display = ui_label(scr, "0", F36, C_TEXT);
    lv_label_set_long_mode(ca_display, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(ca_display, 190);
    lv_obj_set_style_text_align(ca_display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(ca_display, LV_ALIGN_CENTER, 22, -146);
    lv_obj_t *clear = ui_round_btn(scr, 48, C_SURF2, NULL, C_TEXT, calc_key, (void *)(intptr_t)'C');
    lv_obj_center(ui_label(clear, "C", F20, C_TEXT));
    lv_obj_align(clear, LV_ALIGN_CENTER, -112, -146);

    static const char keys[16] = {'7', '8', '9', '/', '4', '5', '6', 'x', '1', '2', '3', '-', '0', '.', '=', '+'};
    lv_color_t c = lv_color_hex(CALC_COLOR);
    for (int i = 0; i < 16; i++) {
        char k = keys[i];
        bool op = i % 4 == 3, eq = k == '=';
        lv_obj_t *b = ui_round_btn(scr, 60, eq ? c : (op ? ui_tint(c, 30) : C_SURF), NULL, C_TEXT, NULL, NULL);
        lv_obj_add_event_cb(b, calc_key, LV_EVENT_PRESSED, (void *)(intptr_t)k);
        lv_obj_set_ext_click_area(b, 2);
        lv_obj_align(b, LV_ALIGN_CENTER, -99 + (i % 4) * 66, -72 + (i / 4) * 66);
        char s[2] = {k, 0};
        lv_obj_center(ui_label(b, s, F24, eq ? lv_color_black() : (op ? c : C_TEXT)));
    }
}

const app_t app_calc = {
    .id = "calc", .name = "Calculator", .icon = &ic_calc_48, .color = CALC_COLOR, .create = calc_create, .no_clock = true,
};

/* ------------------------------------------------------ background duties */

static void duties(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e != EVT_MINUTE) return;
    struct tm now;
    svc_localtime(&now);

    /* Alarm. */
    bool due = A.on && now.tm_hour == A.hour && now.tm_min == A.minute;
    if (due && !A.fired) {
        char buf[16];
        fmt_clock(buf, sizeof(buf), &now, false);
        svc_sound(SND_ALARM);
        shell_alert("Alarm", buf);
    }
    A.fired = due;

    /* Step history: at midnight today's count moves into the week. */
    if (!svc_time_valid()) return;
    if (F.day < 0) F.day = (int16_t)now.tm_yday;
    if (F.day != now.tm_yday) {
        memmove(&F.steps[0], &F.steps[1], sizeof(uint16_t) * 6);
        F.steps[5] = (uint16_t)(S.steps > 65535 ? 65535 : S.steps);
        F.steps[6] = 0;
        F.day = (int16_t)now.tm_yday;
        S.steps = 0;
    }
    F.steps[6] = (uint16_t)(S.steps > 65535 ? 65535 : S.steps);
    if (now.tm_min % 10 == 0) svc_kv_save("fit", &F, sizeof(F));
    fitness_paint();
}

/* Called once at boot: restores saved state and keeps the alarm and step
 * history running whether or not their apps are open. */
void duties_init(void)
{
    svc_kv_load("alarm", &A, sizeof(A));
    A.fired = 0;
    if (A.hour > 23 || A.minute > 59) A.hour = 7, A.minute = 0, A.on = 0;
    if (svc_kv_load("fit", &F, sizeof(F))) {
        struct tm now;
        svc_localtime(&now);
        /* Carry today's steps across a restart, but not across midnight. */
        if (svc_time_valid() && F.day == now.tm_yday) S.steps = F.steps[6];
    } else {
        F.day = -1;
    }
    bus_watch(lv_layer_top(), duties, NULL);
}

#ifdef ORBIT_SIM
void more_sim_preset(void)
{
    static const uint16_t week[7] = {5210, 8420, 3980, 7120, 9050, 6240, 0};
    memcpy(F.steps, week, sizeof(week));
    A.hour = 7, A.minute = 30, A.on = 1;
}
#endif
