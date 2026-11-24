/* The shell: home tiles, app switching, buttons, display rest/wake, quiet
 * modes and the full-screen overlays (notifications, calls, confirmations).
 *
 * Apps are not separate screens. Each one lives in the second page of a
 * two-page horizontal pager that sits on top of the home tiles; the first
 * page is empty. Dragging right therefore slides the app away under the
 * finger and uncovers home, which is the Wear OS swipe-to-dismiss, and LVGL's
 * own scrolling does the tracking and the snap.
 */
#include <stdio.h>
#include <string.h>

#include <math.h>

#include "../assets/gen/logo.h"
#include "ui.h"

#define AOD_BRIGHTNESS 10
#define SAVER_BRIGHTNESS 30
/* After this long resting, waking returns to the watch face, not the last app. */
#define FORGET_APP_MS 90000
#define RECENTS 4

extern const app_t app_timer, app_stopwatch, app_music, app_camera;

static lv_obj_t *home_scr, *tiles, *dots;
static int tile_cols = 2;

static lv_obj_t *pager;     /* the open app's pager, NULL on the home screen */
static lv_obj_t *old_pager; /* the app being replaced, removed once the new one is in */
static const app_t *cur_app;
static const app_t *recents[RECENTS];

static lv_obj_t *rest_scr; /* always-on or dark screen while resting */
static uint32_t rest_since;
static lv_timer_t *tick_timer;

static lv_obj_t *overlay; /* at most one takeover overlay at a time */
static lv_obj_t *lock_veil;
static lv_event_cb_t confirm_yes;

/* ------------------------------------------------------------ rest / wake */

static bool quiet(void)
{
    return CFG.bedtime || S.theatre;
}

int shell_brightness(void)
{
    return CFG.saver && CFG.brightness > SAVER_BRIGHTNESS ? SAVER_BRIGHTNESS : CFG.brightness;
}

static void rest_touched(lv_event_t *e)
{
    LV_UNUSED(e);
    if (quiet() || S.touch_lock) return; /* only a button wakes it */
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_wait_release(indev);
    shell_wake();
}

static void overlay_close(void);

void shell_rest(void)
{
    if (S.display != DISPLAY_ACTIVE) return;
    /* On the charger the always-on screen is free, so show it regardless. */
    bool lit = !quiet() && ((CFG.aod_mode != AOD_OFF && !CFG.saver) || S.batt.plugged);
    S.display = lit ? DISPLAY_AOD : DISPLAY_OFF;
    overlay_close(); /* a forgotten power menu or alert must not sit on the AOD */
    rest_scr = lit ? aod_create() : ui_screen();
    lv_obj_add_flag(rest_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rest_scr, rest_touched, LV_EVENT_PRESSED, NULL);
    lv_screen_load(rest_scr);
    svc_brightness(lit ? AOD_BRIGHTNESS : 0);
    svc_display_active(false);
    ui_timers_rest(true);
    if (tick_timer) lv_timer_set_period(tick_timer, 1000);
    rest_since = svc_millis();
    bus_emit(EVT_DISPLAY, NULL);
}

static void app_drop(void);

void shell_wake(void)
{
    lv_display_trigger_activity(NULL);
    if (S.display == DISPLAY_ACTIVE) return;
    S.display = DISPLAY_ACTIVE;

    if (svc_millis() - rest_since > FORGET_APP_MS && cur_app && !cur_app->keep_awake) {
        app_drop();
        lv_tileview_set_tile_by_index(tiles, 1, 1, LV_ANIM_OFF);
    }
    lv_screen_load(home_scr);
    if (rest_scr) lv_obj_delete_async(rest_scr);
    rest_scr = NULL;
    svc_brightness(shell_brightness());
    svc_display_active(true);
    ui_timers_rest(false);
    home_sync();
    if (tick_timer) lv_timer_set_period(tick_timer, 250);
    bus_emit(EVT_DISPLAY, NULL);
}

void shell_raised(void)
{
    if (CFG.raise_to_wake && !CFG.saver && !quiet() && S.display != DISPLAY_ACTIVE) shell_wake();
}

/* ------------------------------------------------------------------- apps */

const app_t *shell_current(void)
{
    return cur_app;
}

const app_t *shell_recent(int n)
{
    return n >= 0 && n < RECENTS ? recents[n] : NULL;
}

static void remember(const app_t *app)
{
    if (app == &app_launcher || app == &app_faces || app == &app_welcome) return;
    int at = RECENTS - 1;
    for (int i = 0; i < RECENTS; i++) {
        if (recents[i] == app) at = i;
    }
    memmove(&recents[1], &recents[0], sizeof(recents[0]) * (size_t)at);
    recents[0] = app;
}

/* Forget the open app without animation. */
static void app_drop(void)
{
    if (old_pager) lv_obj_delete_async(old_pager);
    if (pager) lv_obj_delete_async(pager);
    old_pager = pager = NULL;
    cur_app = NULL;
}

static void pager_settled(lv_event_t *e)
{
    lv_obj_t *p = lv_event_get_target_obj(e);
    if (p != pager) return;
    if (lv_obj_get_scroll_x(p) < SCR / 2) {
        /* Slid away. An app with pages of its own (Settings) goes up one
         * level and slides back in; otherwise the app is closed. */
        if (cur_app && cur_app->back && cur_app->back()) lv_obj_scroll_to_x(p, SCR, LV_ANIM_ON);
        else app_drop();
    } else if (old_pager) {
        /* Slid in: whatever it replaced can go now, unseen. */
        lv_obj_delete_async(old_pager);
        old_pager = NULL;
    }
}

void shell_open(const app_t *app)
{
    if (!app) return;
    shell_wake();
    if (cur_app == app) return;

    if (old_pager) lv_obj_delete_async(old_pager);
    old_pager = pager;

    pager = ui_box(home_scr, SCR, SCR);
    lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_remove_flag(pager, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(pager, LV_SCROLL_SNAP_START);
    lv_obj_set_style_anim_duration(pager, 180, 0);
    /* Apps that use swipes themselves are closed with the button instead. */
    lv_obj_set_scroll_dir(pager, app->own_gestures ? LV_DIR_NONE : LV_DIR_HOR);

    lv_obj_t *gap = ui_box(pager, SCR, SCR);
    lv_obj_remove_flag(gap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *root = ui_box(pager, SCR, SCR);
    lv_obj_set_pos(root, SCR, 0);
    lv_obj_set_style_bg_color(root, C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, C_TEXT, 0);
    lv_obj_set_style_text_font(root, F20, 0);

    cur_app = app;
    remember(app);
    app->create(root);
    if (!app->no_clock) ui_time_text(root);

    lv_obj_update_layout(pager);
    lv_obj_scroll_to_x(pager, SCR, LV_ANIM_ON); /* slide in from the right */
    lv_obj_add_event_cb(pager, pager_settled, LV_EVENT_SCROLL_END, NULL);
}

static void overlay_close(void)
{
    if (overlay) lv_obj_delete_async(overlay);
    overlay = NULL;
}

void shell_home(void)
{
    overlay_close();
    app_drop();
    lv_tileview_set_tile_by_index(tiles, 1, 1, LV_ANIM_OFF);
}

void shell_back(void)
{
    if (overlay) {
        overlay_close();
        return;
    }
    if (!cur_app) {
        shell_show_tile(1, 1);
        return;
    }
    if (cur_app->back && cur_app->back()) return;
    lv_obj_scroll_to_x(pager, 0, LV_ANIM_ON); /* pager_settled() finishes the job */
}

void shell_show_tile(int col, int row)
{
    home_sync();
    lv_tileview_set_tile_by_index(tiles, (uint32_t)col, (uint32_t)row, LV_ANIM_ON);
}

static bool at_centre_tile(void)
{
    return lv_obj_get_scroll_x(tiles) == SCR && lv_obj_get_scroll_y(tiles) == SCR;
}

bool shell_ongoing(ongoing_t *out)
{
    bool stopwatch = false;
    if (timer_ongoing(out->text, sizeof(out->text), &stopwatch)) {
        out->app = stopwatch ? &app_stopwatch : &app_timer;
        out->icon = stopwatch ? &ic_timer_28 : &ic_hourglass_28;
        return true;
    }
    if (S.cam.status == CAM_READY) {
        out->app = &app_camera, out->icon = &ic_camera_28;
        snprintf(out->text, sizeof(out->text), "%s", S.cam.recording ? "Recording" : "Camera linked");
        return true;
    }
    if (S.media.valid && S.media.playing && S.media.title[0]) {
        out->app = &app_music, out->icon = &ic_music_28;
        snprintf(out->text, sizeof(out->text), "%.27s", S.media.title);
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- overlays */

static void overlay_deleted(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == overlay) overlay = NULL;
}

/* A full-screen takeover on the top layer. Watches are too small for cards. */
static lv_obj_t *overlay_open(void)
{
    overlay_close();
    overlay = ui_box(lv_layer_top(), SCR, SCR);
    lv_obj_set_style_bg_color(overlay, C_BG, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(overlay, C_TEXT, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE); /* swallow touches */
    lv_obj_add_event_cb(overlay, overlay_deleted, LV_EVENT_DELETE, NULL);
    return overlay;
}

static void overlay_dismiss(lv_event_t *e)
{
    LV_UNUSED(e);
    overlay_close();
}

static void overlay_timeout(lv_timer_t *t)
{
    if (lv_timer_get_user_data(t) == overlay) overlay_close();
}

void shell_toast(const char *text)
{
    lv_obj_t *pill = ui_box(lv_layer_top(), LV_SIZE_CONTENT, 48);
    lv_obj_set_style_radius(pill, 24, 0);
    lv_obj_set_style_bg_color(pill, C_SURF2, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(pill, 24, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(ui_label(pill, text, F20, C_TEXT));
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -64);
    lv_obj_delete_delayed(pill, 1800);
}

/* ---- notifications ---- */

static uint32_t detail_id;

static void detail_dismiss(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t id = detail_id;
    overlay_close();
    svc_notif_dismiss(id);
    notif_remove(id);
}

static void detail_reply(lv_event_t *e)
{
    uint32_t id = detail_id;
    svc_notif_reply(id, lv_event_get_user_data(e));
    overlay_close();
    notif_remove(id);
    shell_toast("Reply sent");
}

/* One notification in full: scrolls, and offers what the phone allows. */
void shell_notification(const notif_t *n)
{
    static const char *const builtin[REPLY_MAX] = {"OK", "Yes", "No", "On my way", "Can't talk now"};
    lv_color_t acc = ui_accent();
    detail_id = n->id;
    lv_obj_t *o = overlay_open();
    lv_obj_t *list = ui_list(o);
    lv_obj_set_style_pad_top(list, 70, 0);

    lv_obj_t *app = ui_label_fit(list, n->app, F16, acc, 280);
    lv_obj_set_style_text_letter_space(app, 2, 0);
    lv_obj_t *title = ui_label(list, n->title, F28, C_TEXT);
    lv_obj_set_width(title, 330);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *body = ui_label(list, n->body, F20, C_DIM);
    lv_obj_set_width(body, 320);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_bottom(body, 12, 0);

    /* Quick replies need an Android phone, and a notification that takes them. */
    if (S.link.peer == PEER_ANDROID && !n->no_reply) {
        /* The ones chosen in the Orbit phone app, if it sent any. */
        bool own = S.reply_count > 0;
        for (int i = 0; i < (own ? S.reply_count : REPLY_MAX); i++) {
            const char *text = own ? S.replies[i] : builtin[i];
            lv_obj_t *b = ui_pill_btn(list, text, C_SURF, C_TEXT, detail_reply, (void *)text);
            lv_obj_set_width(b, 300);
            lv_obj_t *label = lv_obj_get_child(b, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_style_max_width(label, 260, 0);
        }
    }
    lv_obj_t *d = ui_pill_btn(list, "Dismiss", ui_tint(acc, 30), acc, detail_dismiss, NULL);
    lv_obj_set_width(d, 300);
    lv_obj_t *c = ui_pill_btn(list, "Close", C_SURF2, C_TEXT, overlay_dismiss, NULL);
    lv_obj_set_width(c, 300);
    ui_list_refresh(list);
}

static void peek_open(lv_event_t *e)
{
    LV_UNUSED(e);
    for (int i = 0; i < S.notif_count; i++) {
        if (S.notifs[i].id != detail_id) continue;
        notif_t copy = S.notifs[i];
        shell_notification(&copy);
        return;
    }
    overlay_close();
}

/* The brief takeover when something new arrives. */
static void peek_notification(const notif_t *n)
{
    lv_obj_t *o = overlay_open();
    lv_color_t acc = ui_accent();
    detail_id = n->id;

    lv_obj_t *badge = ui_round_btn(o, 72, ui_tint(acc, 26), &ic_bell_48, acc, NULL, NULL);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 46);

    lv_obj_t *app = ui_label_fit(o, n->app, F16, acc, 260);
    lv_obj_set_style_text_letter_space(app, 2, 0);
    lv_obj_align(app, LV_ALIGN_TOP_MID, 0, 132);

    lv_obj_t *title = ui_label_fit(o, n->title, F28, C_TEXT, 350);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 162);

    lv_obj_t *body = ui_label(o, n->body, F20, C_DIM);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(body, 350, 4 * lv_font_get_line_height(F20));
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 206);

    lv_obj_t *more = ui_pill_btn(o, "Open", C_SURF2, C_TEXT, peek_open, NULL);
    lv_obj_align(more, LV_ALIGN_BOTTOM_MID, 0, -42);

    lv_obj_add_event_cb(o, overlay_dismiss, LV_EVENT_CLICKED, NULL);
    lv_timer_t *t = ui_timer(o, overlay_timeout, 7000, o);
    lv_timer_set_repeat_count(t, 1);
}

/* ---- calls, confirmations, alerts, power ---- */

static void call_answer(lv_event_t *e)
{
    svc_call(lv_event_get_user_data(e) != NULL);
    overlay_close();
}

static void show_call(void)
{
    lv_obj_t *o = overlay_open();
    lv_obj_align(ui_label(o, "INCOMING CALL", F16, C_DIM), LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_align(ui_label_fit(o, S.call.who[0] ? S.call.who : "Unknown", F36, C_TEXT, 360), LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *no = ui_round_btn(o, 96, C_BAD, &ic_close_48, C_TEXT, call_answer, NULL);
    lv_obj_align(no, LV_ALIGN_CENTER, -80, 90);
    lv_obj_t *yes = ui_round_btn(o, 96, C_GOOD, &ic_check_48, lv_color_black(), call_answer, (void *)1);
    lv_obj_align(yes, LV_ALIGN_CENTER, 80, 90);
}

static void confirm_clicked(lv_event_t *e)
{
    lv_event_cb_t yes = confirm_yes;
    bool accepted = lv_event_get_user_data(e) != NULL;
    overlay_close();
    if (accepted && yes) yes(e);
}

void shell_confirm(const char *question, const char *yes, lv_event_cb_t on_yes)
{
    lv_obj_t *o = overlay_open();
    confirm_yes = on_yes;
    lv_obj_t *q = ui_label(o, question, F28, C_TEXT);
    lv_obj_set_width(q, 340);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(q, LV_ALIGN_CENTER, 0, -70);
    lv_obj_t *y = ui_pill_btn(o, yes, ui_accent(), lv_color_black(), confirm_clicked, (void *)1);
    lv_obj_align(y, LV_ALIGN_CENTER, 0, 44);
    lv_obj_t *n = ui_pill_btn(o, "Cancel", C_SURF2, C_TEXT, confirm_clicked, NULL);
    lv_obj_align(n, LV_ALIGN_CENTER, 0, 116);
}

void shell_alert(const char *title, const char *text)
{
    S.theatre = false;
    shell_wake();
    lv_obj_t *o = overlay_open();
    lv_obj_t *t = ui_label(o, title, F36, C_TEXT);
    lv_obj_set_width(t, 340);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -70);
    lv_obj_t *b = ui_label(o, text, F20, C_DIM);
    lv_obj_set_width(b, 320);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t *ok = ui_pill_btn(o, "OK", ui_accent(), lv_color_black(), overlay_dismiss, NULL);
    lv_obj_align(ok, LV_ALIGN_CENTER, 0, 96);
}

static void do_power_off(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_power_off();
}

static void do_restart(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_restart();
}

static void power_menu(void)
{
    lv_obj_t *o = overlay_open();
    lv_obj_align(ui_label(o, "POWER", F16, C_DIM), LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_t *off = ui_round_btn(o, 104, ui_tint(C_BAD, 30), &ic_power_48, C_BAD, do_power_off, NULL);
    lv_obj_align(off, LV_ALIGN_CENTER, -76, -4);
    lv_obj_align_to(ui_label(o, "Off", F20, C_DIM), off, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_t *re = ui_round_btn(o, 104, C_SURF2, &ic_restart_48, C_TEXT, do_restart, NULL);
    lv_obj_align(re, LV_ALIGN_CENTER, 76, -4);
    lv_obj_align_to(ui_label(o, "Restart", F20, C_DIM), re, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_t *x = ui_pill_btn(o, "Cancel", C_SURF, C_DIM, overlay_dismiss, NULL);
    lv_obj_align(x, LV_ALIGN_BOTTOM_MID, 0, -40);
}

/* ---- touch lock ---- */

static void lock_refresh(void)
{
    if (S.touch_lock && !lock_veil) {
        /* An invisible sheet over everything that swallows every touch. */
        lock_veil = ui_box(lv_layer_sys(), SCR, SCR);
        lv_obj_add_flag(lock_veil, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *hint = ui_label(lock_veil, "Touch locked  " LV_SYMBOL_BULLET "  press power", F16, C_DIM);
        lv_obj_set_style_bg_color(hint, C_SURF, 0);
        lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(hint, 14, 0);
        lv_obj_set_style_pad_ver(hint, 6, 0);
        lv_obj_set_style_radius(hint, 16, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
    } else if (!S.touch_lock && lock_veil) {
        lv_obj_delete(lock_veil);
        lock_veil = NULL;
    }
}

/* ---------------------------------------------------------------- buttons */

void shell_button(int button, int press)
{
    bool was_resting = S.display != DISPLAY_ACTIVE;
    bool was_quiet = S.theatre || S.touch_lock;
    /* Any button press ends theatre mode and touch lock. */
    S.theatre = false, S.touch_lock = false;
    lock_refresh();
    shell_wake();
    if (was_quiet) {
        bus_emit(EVT_SETTINGS, NULL);
        return;
    }

    if (button == BTN_POWER) {
        if (press == PRESS_LONG) power_menu();
        else if (was_resting) return;
        else if (overlay || cur_app) shell_back();
        else if (!at_centre_tile()) shell_show_tile(1, 1);
        else shell_rest();
        return;
    }
    /* Side button: apps on a press, last app on a double press, the
     * assistant on a hold. */
    if (press == PRESS_LONG) {
        overlay_close();
        agent_listen_on_open();
        shell_open(app_find("agent"));
    } else if (press == PRESS_DOUBLE) {
        const app_t *last = recents[0] == cur_app ? recents[1] : recents[0];
        overlay_close();
        if (last) shell_open(last);
    } else if (!was_resting) {
        if (cur_app || overlay) shell_home();
        else if (!at_centre_tile()) shell_show_tile(1, 1);
        else shell_open(&app_launcher);
    }
}

/* ------------------------------------------------------------------- tick */

static void on_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(user);
    if (e == EVT_NOTIF && data && !CFG.dnd && !quiet()) {
        shell_wake();
        if (!S.call.active && CFG.previews) peek_notification(data);
        if (CFG.sound) svc_sound(SND_NOTIFY);
    } else if (e == EVT_CALL) {
        if (S.call.active) {
            S.theatre = false;
            shell_wake();
            show_call();
        } else {
            overlay_close();
        }
    } else if (e == EVT_SETTINGS) {
        lock_refresh();
        if (S.display == DISPLAY_ACTIVE) {
            svc_brightness(shell_brightness());
            if (quiet()) shell_rest(); /* bedtime or theatre just switched on */
        }
    }
}

static void tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    static int last_sec = -1, last_min = -1;
    struct tm now;
    svc_localtime(&now);

    if (S.display == DISPLAY_ACTIVE) {
        if (now.tm_sec != last_sec) bus_emit(EVT_SECOND, &now);
        bool hold = (cur_app && cur_app->keep_awake) || S.call.active;
        if (!hold && lv_display_get_inactive_time(NULL) > (uint32_t)CFG.timeout_s * 1000) shell_rest();
    }
    if (now.tm_min != last_min) {
        bus_emit(EVT_MINUTE, &now);
        if (S.display == DISPLAY_AOD && rest_scr) aod_tick(rest_scr);
    }
    last_sec = now.tm_sec, last_min = now.tm_min;
}

/* ------------------------------------------------------------------ tiles */

static void tile_scroll_begin(lv_event_t *e)
{
    LV_UNUSED(e);
    home_sync(); /* a tile that skipped refreshes must be fresh before it slides in */
}

static void tile_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    home_sync();
    lv_obj_t *t = lv_tileview_get_tile_active(tiles);
    int col = t ? lv_obj_get_x(t) / SCR : 1, row = t ? lv_obj_get_y(t) / SCR : 1;
    if (row != 1 || col == 1) {
        lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < tile_cols; i++) lv_obj_set_style_bg_color(lv_obj_get_child(dots, i), i == col ? C_TEXT : C_FAINT, 0);
}

static lv_obj_t *add_tile(int col, int row, lv_dir_t dir)
{
    /* Not lv_obj_remove_style_all(): a tile's grid position is itself a style. */
    lv_obj_t *t = lv_tileview_add_tile(tiles, (uint8_t)col, (uint8_t)row, dir);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_style_radius(t, 0, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    return t;
}

/* The Wear OS map: down for quick settings, up for notifications, sideways
 * for tiles. Apps live behind the side button.
 *
 *                 quick
 *   tile 0    watch face    tile 1   tile 2 ...
 *              notifications                      */
static void build_tiles(void)
{
    tiles = lv_tileview_create(home_scr);
    lv_obj_remove_style_all(tiles);
    lv_obj_set_size(tiles, SCR, SCR);
    lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_anim_duration(tiles, 180, 0);
    lv_obj_move_to_index(tiles, 0); /* behind any open app */

    int n = 0;
    while (n < TILE_SLOTS && CFG.tiles[n] != TILE_NONE) n++;
    tile_cols = n > 0 ? n + 1 : 2;

    quick_create(add_tile(1, 0, LV_DIR_BOTTOM));
    if (n > 0) tile_create(add_tile(0, 1, LV_DIR_RIGHT), CFG.tiles[0]);
    else lv_obj_remove_flag(add_tile(0, 1, LV_DIR_RIGHT), LV_OBJ_FLAG_SNAPPABLE);
    watchface_create(add_tile(1, 1, n > 0 ? LV_DIR_ALL : (LV_DIR_VER | LV_DIR_RIGHT)));
    for (int i = 1; i < n; i++) tile_create(add_tile(i + 1, 1, i == n - 1 ? LV_DIR_LEFT : LV_DIR_HOR), CFG.tiles[i]);
    notifs_create(add_tile(1, 2, LV_DIR_TOP));

    /* Page dots, shown only on the side tiles. */
    dots = ui_box(home_scr, tile_cols * 20, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(dots, 1);
    for (int i = 0; i < tile_cols; i++) {
        lv_obj_t *d = ui_box(dots, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_pos(d, 6 + i * 20, 2);
    }
    lv_obj_add_event_cb(tiles, tile_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(tiles, tile_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);

    lv_obj_update_layout(tiles);
    lv_tileview_set_tile_by_index(tiles, 1, 1, LV_ANIM_OFF);
    tile_changed(NULL);
}

void shell_rebuild_tiles(void)
{
    lv_obj_delete(tiles);
    lv_obj_delete(dots);
    build_tiles();
}

/* ------------------------------------------------------------------- boot */

#define BOOT_Y (-24)
static lv_obj_t *boot_body;

static void boot_ring(void *ring, int32_t v)
{
    LV_UNUSED(ring);
    float a = (-45.0f - 360.0f * (float)(1000 - v) / 1000.0f) * 0.01745329f;
    lv_obj_align(boot_body, LV_ALIGN_CENTER, (int)lroundf(LOGO_ORBIT_PX * cosf(a)), (int)lroundf(LOGO_ORBIT_PX * sinf(a)) + BOOT_Y);
}

static void boot_done(lv_anim_t *a)
{
    lv_obj_delete(lv_obj_get_parent(a->var));
    if (!CFG.setup_done) shell_open(&app_welcome);
}

/* The logo: its body goes once round the ring, settles into its gap, and
 * the whole thing gets out of the way. */
static void boot_animation(void)
{
    lv_obj_t *veil = ui_box(lv_layer_top(), SCR, SCR);
    lv_obj_set_style_bg_color(veil, C_BG, 0);
    lv_obj_set_style_bg_opa(veil, LV_OPA_COVER, 0);
    lv_obj_add_flag(veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ring = lv_image_create(veil);
    lv_image_set_src(ring, &logo_ring);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, BOOT_Y);
    boot_body = ui_icon(veil, &logo_dot, lv_color_hex(LOGO_BODY_COLOR));
    lv_obj_align(ui_label(veil, "Orbit", F36, C_TEXT), LV_ALIGN_CENTER, 0, 96);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1300);
    lv_anim_set_exec_cb(&a, boot_ring);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, boot_done);
    lv_anim_start(&a);
}

void shell_init(void)
{
    home_scr = ui_screen();
    build_tiles();
    lv_screen_load(home_scr);

    S.display = DISPLAY_ACTIVE;
    svc_brightness(shell_brightness());
    bus_watch(home_scr, on_event, NULL);
    tick_timer = lv_timer_create(tick, 250, NULL);
    boot_animation();
}

void orbit_boot(void)
{
    /* The calendar is too big for the chip's own memory, so it is given
     * room in the external RAM once and keeps it. */
    S.agenda = lv_malloc_zeroed(sizeof(agenda_item_t) * AGENDA_MAX);
    S.day.items = lv_malloc_zeroed(sizeof(agenda_item_t) * DAY_MAX);
    settings_load();
    macros_load();
    custom_faces_load();
    duties_init(); /* restores alarm and step state before the UI reads it */
    shell_init();
}
