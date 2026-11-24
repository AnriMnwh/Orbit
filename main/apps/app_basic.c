/* Torch and Timer. */
#include <stdio.h>

#include "../ui/ui.h"

/* ------------------------------------------------------------------ torch */

static int torch_color, torch_step;
static bool torch_sos;

static void torch_paint(lv_obj_t *scr)
{
    static const uint32_t colors[] = {0xFFFFFF, 0xFF2A1A, 0xFFB45A};
    lv_obj_set_style_bg_color(scr, lv_color_hex(colors[torch_color % 3]), 0);
}

static void torch_tap(lv_event_t *e)
{
    torch_sos = false;
    torch_color++;
    torch_paint(lv_event_get_target_obj(e));
}

/* Hold to flash SOS in Morse: three short, three long, three short. */
static void torch_hold(lv_event_t *e)
{
    LV_UNUSED(e);
    torch_sos = true;
    torch_step = 0;
    shell_toast("SOS");
}

static void torch_blink(lv_timer_t *t)
{
    /* One character per 200 ms: '1' lit, '0' dark. */
    static const char pattern[] = "101010001110111011100010101000000";
    lv_obj_t *scr = lv_timer_get_user_data(t);
    if (!torch_sos) return;
    bool lit = pattern[torch_step] == '1';
    torch_step = (torch_step + 1) % (int)(sizeof(pattern) - 1);
    if (lit) torch_paint(scr);
    else lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
}

static void torch_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.display == DISPLAY_ACTIVE) svc_brightness(shell_brightness()); /* keeps the battery-saver cap */
}

static void torch_create(lv_obj_t *scr)
{
    torch_color = 0, torch_sos = false;
    torch_paint(scr);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, torch_tap, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, torch_hold, LV_EVENT_LONG_PRESSED, NULL);
    ui_timer(scr, torch_blink, 200, scr);
    lv_obj_add_event_cb(scr, torch_closed, LV_EVENT_DELETE, NULL);
    svc_brightness(100);
}

const app_t app_torch = {
    .id = "torch", .name = "Flashlight", .icon = &ic_torch_48, .color = 0xFDD663,
    .create = torch_create, .keep_awake = true, .no_clock = true,
};

/* ------------------------------------------------------------------ timer */

/* The countdown and stopwatch keep running after the app is closed; only the
 * widgets below belong to the screen. */
static struct {
    int set_s;          /* chosen duration */
    bool running;
    uint32_t end_ms;    /* when running */
    int left_ms;        /* when paused */
    bool sw_mode;       /* showing the stopwatch */
    bool sw_running;
    uint32_t sw_start_ms;
    uint32_t sw_acc_ms;
    lv_timer_t *watch;  /* fires the alarm even with the app closed */
} T = {.set_s = 300, .left_ms = 300000};

static lv_obj_t *t_scr, *t_title, *t_time, *t_arc, *t_go, *t_left, *t_right;

static int timer_left_ms(void)
{
    if (!T.running) return T.left_ms;
    int32_t d = (int32_t)(T.end_ms - svc_millis());
    return d > 0 ? d : 0;
}

static uint32_t stopwatch_ms(void)
{
    return T.sw_acc_ms + (T.sw_running ? svc_millis() - T.sw_start_ms : 0);
}

static void timer_paint(void)
{
    if (!t_scr) return;
    char buf[24];
    lv_color_t c = lv_color_hex(0xFF7A45);
    if (T.sw_mode) {
        uint32_t ms = stopwatch_ms();
        snprintf(buf, sizeof(buf), "%02u:%02u.%u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60), (unsigned)(ms / 100 % 10));
        ui_arc_set(t_arc, (int)(ms / 1000 % 60 * 100 / 60));
        ui_icon_set(ui_btn_icon(t_go), T.sw_running ? &ic_pause_48 : &ic_play_48, lv_color_black());
        ui_icon_set(ui_btn_icon(t_left), &ic_restart_48, C_TEXT);
        lv_obj_add_flag(t_right, LV_OBJ_FLAG_HIDDEN);
    } else {
        int left = timer_left_ms();
        int s = (left + 999) / 1000;
        snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
        ui_arc_set(t_arc, T.set_s ? left / 10 / T.set_s : 0);
        ui_icon_set(ui_btn_icon(t_go), T.running ? &ic_pause_48 : &ic_play_48, lv_color_black());
        bool fresh = !T.running && T.left_ms == T.set_s * 1000;
        ui_icon_set(ui_btn_icon(t_left), fresh ? &ic_minus_48 : &ic_restart_48, C_TEXT);
        if (fresh) lv_obj_remove_flag(t_right, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(t_right, LV_OBJ_FLAG_HIDDEN);
    }
    ui_label_set(t_time, buf);
    ui_label_set(t_title, T.sw_mode ? "STOPWATCH" : "TIMER");
    lv_obj_set_style_arc_color(t_arc, c, LV_PART_INDICATOR);
}

static void timer_watch(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (T.running && timer_left_ms() == 0) {
        T.running = false;
        T.left_ms = T.set_s * 1000;
        svc_sound(SND_ALARM);
        shell_alert("Time's up", "Your timer has finished.");
    }
    if (!T.running && T.watch) {
        lv_timer_delete(T.watch);
        T.watch = NULL;
    }
    if (S.display == DISPLAY_ACTIVE || !T.running) timer_paint(); /* a running readout is parked while resting */
}

static void timer_go(lv_event_t *e)
{
    LV_UNUSED(e);
    if (T.sw_mode) {
        if (T.sw_running) T.sw_acc_ms = stopwatch_ms();
        else T.sw_start_ms = svc_millis();
        T.sw_running = !T.sw_running;
    } else if (T.running) {
        T.left_ms = timer_left_ms();
        T.running = false;
    } else if (T.left_ms > 0) {
        T.end_ms = svc_millis() + (uint32_t)T.left_ms;
        T.running = true;
        if (!T.watch) T.watch = lv_timer_create(timer_watch, 1000, NULL);
    }
    timer_paint();
}

/* For the chip on the watch face: is a countdown or stopwatch running? */
bool timer_ongoing(char *text, size_t len, bool *stopwatch)
{
    if (T.running) {
        int s = (timer_left_ms() + 999) / 1000;
        snprintf(text, len, "%02d:%02d", s / 60, s % 60);
        *stopwatch = false;
        return true;
    }
    if (T.sw_running) {
        uint32_t s = stopwatch_ms() / 1000;
        snprintf(text, len, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
        *stopwatch = true;
        return true;
    }
    return false;
}

/* Starts a countdown from outside the app (the assistant uses this). */
void timer_start(int seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 99 * 60) seconds = 99 * 60;
    T.sw_mode = false;
    T.set_s = seconds;
    T.left_ms = seconds * 1000;
    T.end_ms = svc_millis() + (uint32_t)T.left_ms;
    T.running = true;
    if (!T.watch) T.watch = lv_timer_create(timer_watch, 1000, NULL);
    timer_paint();
}

static void timer_adjust(int delta_s)
{
    T.set_s += delta_s;
    if (T.set_s < 60) T.set_s = 60;
    if (T.set_s > 99 * 60) T.set_s = 99 * 60;
    T.left_ms = T.set_s * 1000;
}

static void timer_left_btn(lv_event_t *e)
{
    LV_UNUSED(e);
    if (T.sw_mode) {
        T.sw_running = false, T.sw_acc_ms = 0;
    } else if (!T.running && T.left_ms == T.set_s * 1000) {
        timer_adjust(-60);
    } else {
        T.running = false;
        T.left_ms = T.set_s * 1000;
    }
    timer_paint();
}

static void timer_right_btn(lv_event_t *e)
{
    LV_UNUSED(e);
    timer_adjust(60);
    timer_paint();
}

static void timer_refresh(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (T.running || T.sw_running) timer_paint();
}

static void timer_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    t_scr = NULL;
}

/* Timer and Stopwatch are two apps, as on a Pixel Watch, sharing one screen. */
static void clock_screen(lv_obj_t *scr, bool stopwatch)
{
    T.sw_mode = stopwatch;
    t_scr = scr;
    lv_obj_add_event_cb(scr, timer_closed, LV_EVENT_DELETE, NULL);

    t_arc = ui_arc(scr, 442, 10, C_SURF, lv_color_hex(0xFF7A45), 270, 360);
    lv_obj_center(t_arc);

    t_title = ui_label(scr, "", F16, C_DIM);
    lv_obj_set_style_text_letter_space(t_title, 2, 0);
    lv_obj_align(t_title, LV_ALIGN_TOP_MID, 0, 62);

    t_time = ui_label(scr, "", &font_num_big, C_TEXT);
    lv_obj_align(t_time, LV_ALIGN_CENTER, 0, -34);

    t_left = ui_round_btn(scr, 72, C_SURF2, &ic_minus_48, C_TEXT, timer_left_btn, NULL);
    lv_obj_align(t_left, LV_ALIGN_CENTER, -106, 88);
    t_go = ui_round_btn(scr, 96, lv_color_hex(0xFF7A45), &ic_play_48, lv_color_black(), timer_go, NULL);
    lv_obj_align(t_go, LV_ALIGN_CENTER, 0, 88);
    t_right = ui_round_btn(scr, 72, C_SURF2, &ic_plus_48, C_TEXT, timer_right_btn, NULL);
    lv_obj_align(t_right, LV_ALIGN_CENTER, 106, 88);

    ui_timer_visual(scr, timer_refresh, 100, NULL);
    timer_paint();
}

/* Not "timer_create": the C library already has a function of that name. */
static void timer_app_create(lv_obj_t *scr)
{
    clock_screen(scr, false);
}

static void stopwatch_app_create(lv_obj_t *scr)
{
    clock_screen(scr, true);
}

const app_t app_timer = {
    .id = "timer", .name = "Timer", .icon = &ic_hourglass_48, .color = 0xFF7A45, .create = timer_app_create,
};

const app_t app_stopwatch = {
    .id = "stopwatch", .name = "Stopwatch", .icon = &ic_timer_48, .color = 0xFFB68C, .create = stopwatch_app_create,
};
