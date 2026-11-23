/* Everything around the watch face: the app list, quick settings,
 * notifications and the tiles. */
#include <stdio.h>
#include <string.h>

#include "ui.h"

/* ---------------------------------------------------------------- launcher */

#define ROW_H 104

/* Rows hug the curve of the display: the further a row is from the middle,
 * the more it is pushed in and faded, like text on a wheel. */
static void launcher_curve(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_target_obj(e);
    lv_area_t la;
    lv_obj_get_coords(list, &la);
    int mid = (la.y1 + la.y2) / 2;
    uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t)i);
        lv_area_t ra;
        lv_obj_get_coords(row, &ra);
        int d = LV_ABS((ra.y1 + ra.y2) / 2 - mid);
        if (d > 233) d = 233;
        /* x on a circle of radius 233: r - sqrt(r^2 - d^2) */
        lv_sqrt_res_t s;
        lv_sqrt((uint32_t)(233 * 233 - d * d), &s, 0x8000);
        lv_obj_set_style_translate_x(row, 233 - s.i, 0);
        lv_obj_set_style_opa(lv_obj_get_child(row, 1), (lv_opa_t)(255 - d * 190 / 233), 0);
    }
}

static void launcher_open(lv_event_t *e)
{
    shell_open(lv_event_get_user_data(e));
}

static void launcher_row(lv_obj_t *list, const app_t *app)
{
    lv_color_t c = lv_color_hex(app->color);
    lv_obj_t *row = ui_box(list, 330, ROW_H);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, launcher_open, LV_EVENT_SHORT_CLICKED, (void *)app);

    lv_obj_t *dot = ui_round_btn(row, 84, ui_tint(c, 24), app->icon, c, NULL, NULL);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *name = ui_label(row, app->name, F28, C_TEXT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 108, 0);
}

void launcher_create(lv_obj_t *tile)
{
    lv_obj_t *list = ui_box(tile, SCR, SCR);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    /* Padding lets the first and last rows reach the middle of the screen. */
    lv_obj_set_style_pad_ver(list, (SCR - ROW_H) / 2, 0);
    lv_obj_set_style_pad_left(list, 56, 0);
    lv_obj_add_event_cb(list, launcher_curve, LV_EVENT_SCROLL, NULL);

    /* Recently used apps first, then everything in alphabetical order. */
    const app_t *recent[2] = {shell_recent(0), shell_recent(1)};
    for (int i = 0; i < 2; i++) {
        if (recent[i]) launcher_row(list, recent[i]);
    }
    for (int i = 0; i < APP_COUNT; i++) {
        if (APPS[i] != recent[0] && APPS[i] != recent[1]) launcher_row(list, APPS[i]);
    }
    lv_obj_update_layout(list);
    lv_obj_scroll_to_view(lv_obj_get_child(list, 1), LV_ANIM_OFF);
    lv_obj_send_event(list, LV_EVENT_SCROLL, NULL);
}

/* The app list is its own screen, opened with the side button as on Wear OS. */
const app_t app_launcher = {
    .id = "apps", .name = "Apps", .icon = &ic_grid_48, .color = 0xFFFFFF, .create = launcher_create, .no_clock = true,
};

/* ---------------------------------------------------------- quick settings */

enum {
    Q_SLEEP, Q_AOD, Q_DND,
    Q_SETTINGS, Q_TORCH, Q_SAVER,
    Q_BEDTIME, Q_THEATRE, Q_LOCK,
    Q_FIND, Q_KEYS, Q_APPS,
    Q_COUNT,
};

static lv_obj_t *q_batt, *q_link, *q_slider, *q_btn[Q_COUNT];
static bool q_ringing;

static bool quick_state(int id)
{
    switch (id) {
    case Q_DND: return CFG.dnd;
    case Q_BEDTIME: return CFG.bedtime;
    case Q_FIND: return q_ringing;
    case Q_SAVER: return CFG.saver;
    case Q_AOD: return CFG.aod_mode != AOD_OFF;
    case Q_KEYS: return S.link.keys_mode;
    default: return false;
    }
}

static void quick_refresh(void)
{
    char buf[48];
    if (S.batt.percent < 0) snprintf(buf, sizeof(buf), "Battery --");
    else snprintf(buf, sizeof(buf), "%d%%%s", S.batt.percent, S.batt.charging ? "  charging" : "");
    lv_label_set_text(q_batt, buf);

    if (S.link.keys_mode) snprintf(buf, sizeof(buf), "Keyboard mode");
    else if (S.link.midi_mode) snprintf(buf, sizeof(buf), "MIDI mode");
    else if (S.link.status == LINK_CONNECTED) snprintf(buf, sizeof(buf), "%s", S.link.name[0] ? S.link.name : "Phone connected");
    else snprintf(buf, sizeof(buf), "Phone not connected");
    lv_label_set_text(q_link, buf);

    lv_color_t acc = ui_accent();
    for (int i = 0; i < Q_COUNT; i++) {
        bool on = quick_state(i);
        lv_obj_set_style_bg_color(q_btn[i], on ? acc : C_SURF2, 0);
        ui_icon_set(ui_btn_icon(q_btn[i]), NULL, on ? lv_color_black() : C_TEXT);
    }
    lv_slider_set_value(q_slider, CFG.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(q_slider, acc, LV_PART_INDICATOR);
}

static void quick_tap(lv_event_t *e)
{
    static const char *const aod[AOD_COUNT] = {"Always-on off", "Always-on clock", "Always-on photos"};
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    switch (id) {
    case Q_SLEEP:
        shell_home();
        shell_rest();
        return;
    case Q_DND:
        CFG.dnd = !CFG.dnd;
        shell_toast(CFG.dnd ? "Do not disturb on" : "Do not disturb off");
        break;
    case Q_BEDTIME:
        CFG.bedtime = !CFG.bedtime;
        if (!CFG.bedtime) shell_toast("Bedtime mode off");
        break;
    case Q_THEATRE:
        S.theatre = true; /* a button press ends it */
        break;
    case Q_TORCH: shell_open(app_find("torch")); return;
    case Q_FIND:
        if (S.link.status != LINK_CONNECTED || S.link.peer != PEER_ANDROID) {
            shell_toast(S.link.peer == PEER_IPHONE ? "Not available on iPhone" : "Phone not connected");
            return;
        }
        q_ringing = !q_ringing;
        svc_find_phone(q_ringing);
        quick_refresh();
        return;
    case Q_SAVER:
        CFG.saver = !CFG.saver;
        shell_toast(CFG.saver ? "Battery saver on" : "Battery saver off");
        break;
    case Q_LOCK:
        S.touch_lock = true;
        break;
    case Q_AOD:
        CFG.aod_mode = (uint8_t)((CFG.aod_mode + 1) % AOD_COUNT);
        shell_toast(aod[CFG.aod_mode]);
        break;
    case Q_KEYS: svc_link_keys_mode(!S.link.keys_mode); return;
    case Q_APPS: shell_open(&app_launcher); return;
    case Q_SETTINGS: shell_open(app_find("settings")); return;
    }
    settings_changed();
}

static void quick_brightness(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target_obj(e);
    CFG.brightness = (uint8_t)lv_slider_get_value(s);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_changed();
    else svc_brightness(CFG.brightness);
}

static void quick_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_BATTERY || e == EVT_LINK || e == EVT_SETTINGS) quick_refresh();
}

void quick_create(lv_obj_t *tile)
{
    static const lv_image_dsc_t *const icons[Q_COUNT] = {
        [Q_SLEEP] = &ic_power_48,   [Q_AOD] = &ic_clock_48,     [Q_DND] = &ic_moon_48,
        [Q_SETTINGS] = &ic_gear_48, [Q_TORCH] = &ic_torch_48,   [Q_SAVER] = &ic_bolt_48,
        [Q_BEDTIME] = &ic_zzz_48,   [Q_THEATRE] = &ic_half_48,  [Q_LOCK] = &ic_drop_48,
        [Q_FIND] = &ic_target_48,   [Q_KEYS] = &ic_keys_48,     [Q_APPS] = &ic_grid_48,
    };
    /* The panel is taller than the screen and scrolls; at its end the same
     * drag carries on into the watch face. */
    lv_obj_t *col = ui_box(tile, SCR, SCR);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    q_batt = ui_label(col, "", F24, C_TEXT);
    lv_obj_align(q_batt, LV_ALIGN_TOP_MID, 0, 38);
    q_link = ui_label_fit(col, "", F16, C_DIM, 250);
    lv_obj_align(q_link, LV_ALIGN_TOP_MID, 0, 72);

    /* Brightness slider right at the top of the control centre so it is
     * immediately reachable without scrolling. */
    lv_obj_t *sun = ui_icon(col, &ic_sun_28, C_DIM);
    lv_obj_set_pos(sun, 96, 110);
    q_slider = lv_slider_create(col);
    lv_obj_remove_style_all(q_slider);
    lv_obj_set_size(q_slider, 220, 14);
    lv_obj_set_pos(q_slider, 140, 117);
    lv_slider_set_range(q_slider, 5, 100);
    lv_obj_set_style_radius(q_slider, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(q_slider, C_SURF2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(q_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(q_slider, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(q_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(q_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(q_slider, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(q_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(q_slider, 9, LV_PART_KNOB);
    lv_obj_set_ext_click_area(q_slider, 22);
    lv_obj_add_event_cb(q_slider, quick_brightness, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(q_slider, quick_brightness, LV_EVENT_RELEASED, NULL);

    const int sz = 88, gap = 104;
    for (int i = 0; i < Q_COUNT; i++) {
        q_btn[i] = ui_round_btn(col, sz, C_SURF2, icons[i], C_TEXT, quick_tap, (void *)(intptr_t)i);
        lv_obj_set_ext_click_area(q_btn[i], 4);
        int x = SCR_C - sz / 2 + (i % 3 - 1) * gap;
        lv_obj_set_pos(q_btn[i], x, 162 + (i / 3) * gap);
    }
    /* Room to scroll the last row comfortably into the middle. */
    lv_obj_set_pos(ui_box(col, 10, 10), 0, 640);

    bus_watch(tile, quick_event, NULL);
    quick_refresh();
}

/* ----------------------------------------------------------- notifications */

static lv_obj_t *n_list;

static uint32_t card_id(lv_obj_t *card)
{
    return (uint32_t)(uintptr_t)lv_obj_get_user_data(card);
}

/* Cards follow a sideways drag and are dismissed past a third of the way;
 * a plain tap opens the notification in full. */
static void card_event(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_target_obj(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    int tx = lv_obj_get_style_translate_x(card, 0);

    if (code == LV_EVENT_PRESSING && indev && !lv_indev_get_scroll_obj(indev)) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        tx += v.x;
        if (LV_ABS(tx) < 6 && LV_ABS(v.x) <= LV_ABS(v.y)) return;
        lv_obj_set_style_translate_x(card, tx, 0);
        lv_obj_set_style_bg_opa(card, (lv_opa_t)(255 - LV_MIN(LV_ABS(tx), 200)), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (LV_ABS(tx) > 120) {
            uint32_t id = card_id(card);
            svc_notif_dismiss(id);
            notif_remove(id);
        } else if (tx != 0) {
            lv_obj_set_style_translate_x(card, 0, 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED && LV_ABS(tx) < 6) {
        uint32_t id = card_id(card);
        for (int i = 0; i < S.notif_count; i++) {
            if (S.notifs[i].id != id) continue;
            notif_t copy = S.notifs[i];
            shell_notification(&copy);
            return;
        }
    }
}

static void notif_clear_all(lv_event_t *e)
{
    LV_UNUSED(e);
    for (int i = 0; i < S.notif_count; i++) svc_notif_dismiss(S.notifs[i].id);
    notif_clear();
}

static void ago(char *buf, size_t len, time_t when)
{
    long d = (long)(svc_time() - when);
    if (d < 60) snprintf(buf, len, "now");
    else if (d < 3600) snprintf(buf, len, "%ldm", d / 60);
    else if (d < 86400) snprintf(buf, len, "%ldh", d / 3600);
    else snprintf(buf, len, "%ldd", d / 86400);
}

static void notifs_refresh(void)
{
    lv_obj_clean(n_list);
    if (S.notif_count == 0) {
        lv_obj_t *empty = ui_box(n_list, 330, 300);
        lv_obj_t *icon = ui_icon(empty, &ic_bell_48, C_FAINT);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -24);
        lv_obj_align(ui_label(empty, "All caught up", F24, C_DIM), LV_ALIGN_CENTER, 0, 34);
        ui_list_refresh(n_list);
        return;
    }
    lv_color_t acc = ui_accent();
    for (int i = 0; i < S.notif_count; i++) {
        const notif_t *n = &S.notifs[i];
        lv_obj_t *card = ui_box(n_list, 330, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(card, 30, 0);
        lv_obj_set_style_bg_color(card, C_SURF, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(card, 20, 0);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(card, (void *)(uintptr_t)n->id);
        lv_obj_add_event_cb(card, card_event, LV_EVENT_ALL, NULL);

        char when[12], head[48];
        ago(when, sizeof(when), n->when);
        snprintf(head, sizeof(head), "%s  " LV_SYMBOL_BULLET "  %s", n->app, when);
        lv_obj_t *app = ui_label(card, head, F16, acc);
        lv_label_set_long_mode(app, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(app, 290);

        lv_obj_t *title = ui_label(card, n->title, F24, C_TEXT);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(title, 290, lv_font_get_line_height(F24));

        if (n->body[0]) {
            lv_obj_t *body = ui_label(card, n->body, F20, C_DIM);
            lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(body, 290);
            lv_obj_set_style_max_height(body, 3 * lv_font_get_line_height(F20), 0);
        }
    }
    lv_obj_t *clear = ui_pill_btn(n_list, "Clear all", C_SURF2, C_TEXT, notif_clear_all, NULL);
    lv_obj_set_style_margin_top(clear, 8, 0);
    ui_list_refresh(n_list);
}

static void notifs_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_NOTIF || e == EVT_MINUTE) notifs_refresh();
}

void notifs_create(lv_obj_t *tile)
{
    n_list = ui_list(tile);
    bus_watch(tile, notifs_event, NULL);
    notifs_refresh();
}

/* ------------------------------------------------------------------ tiles */

/* ---- music ---- */

static lv_obj_t *g_title, *g_artist, *g_play;

static void glance_refresh(void)
{
    bool have = S.media.valid && S.media.title[0];
    lv_label_set_text(g_title, have ? S.media.title : "Nothing playing");
    lv_label_set_text(g_artist, have ? S.media.artist : "Play something on your phone");
    ui_icon_set(ui_btn_icon(g_play), S.media.playing ? &ic_pause_48 : &ic_play_48, lv_color_black());
}

static void glance_cmd(lv_event_t *e)
{
    svc_media((media_cmd_t)(uintptr_t)lv_event_get_user_data(e));
}

static void tile_open(lv_event_t *e)
{
    shell_open(app_find(lv_event_get_user_data(e)));
}

static void glance_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_MEDIA) glance_refresh();
}

static void music_tile(lv_obj_t *tile)
{
    lv_color_t c = lv_color_hex(0xFF6B9A);
    lv_obj_align(ui_icon(tile, &ic_music_28, c), LV_ALIGN_TOP_MID, 0, 44);
    g_title = ui_label_fit(tile, "", F28, C_TEXT, 330);
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, -92);
    g_artist = ui_label_fit(tile, "", F20, C_DIM, 300);
    lv_obj_align(g_artist, LV_ALIGN_CENTER, 0, -56);

    lv_obj_t *prev = ui_round_btn(tile, 76, C_SURF2, &ic_prev_48, C_TEXT, glance_cmd, (void *)MEDIA_PREV);
    lv_obj_align(prev, LV_ALIGN_CENTER, -112, 36);
    g_play = ui_round_btn(tile, 104, c, &ic_play_48, lv_color_black(), glance_cmd, (void *)MEDIA_TOGGLE);
    lv_obj_align(g_play, LV_ALIGN_CENTER, 0, 36);
    lv_obj_t *next = ui_round_btn(tile, 76, C_SURF2, &ic_next_48, C_TEXT, glance_cmd, (void *)MEDIA_NEXT);
    lv_obj_align(next, LV_ALIGN_CENTER, 112, 36);

    lv_obj_t *more = ui_pill_btn(tile, "Open", C_SURF, C_DIM, tile_open, "music");
    lv_obj_align(more, LV_ALIGN_BOTTOM_MID, 0, -40);
    bus_watch(tile, glance_event, NULL);
    glance_refresh();
}

/* ---- activity ---- */


static lv_obj_t *act_arc, *act_steps, *act_goal, *act_batt;

static void activity_refresh(void)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%u", (unsigned)S.steps);
    lv_label_set_text(act_steps, buf);
    ui_arc_set(act_arc, (int)(S.steps * 100 / CFG.step_goal));
    lv_obj_set_style_arc_color(act_arc, ui_accent(), LV_PART_INDICATOR);
    if (S.steps >= CFG.step_goal) snprintf(buf, sizeof(buf), "Goal reached");
    else snprintf(buf, sizeof(buf), "%u to go", (unsigned)(CFG.step_goal - S.steps));
    lv_label_set_text(act_goal, buf);
    if (S.batt.percent < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", S.batt.percent);
    lv_label_set_text(act_batt, buf);
}

static void activity_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_MINUTE || e == EVT_BATTERY || e == EVT_SETTINGS) activity_refresh();
}

static void activity_tile(lv_obj_t *tile)
{
    /* Progress hugs the bezel, the number sits in the middle. */
    act_arc = ui_arc(tile, 436, 14, C_SURF, ui_accent(), 135, 270);
    lv_obj_center(act_arc);
    lv_obj_align(ui_icon(tile, &ic_steps_48, C_DIM), LV_ALIGN_CENTER, 0, -112);
    act_steps = ui_label(tile, "", &font_num_big, C_TEXT);
    lv_obj_align(act_steps, LV_ALIGN_CENTER, 0, -28);
    lv_obj_align(ui_label(tile, "STEPS TODAY", F16, C_DIM), LV_ALIGN_CENTER, 0, 34);
    act_goal = ui_label(tile, "", F20, C_DIM);
    lv_obj_align(act_goal, LV_ALIGN_CENTER, 0, 66);

    lv_obj_t *chip = ui_pill_btn(tile, "", C_SURF, C_TEXT, tile_open, "fitness");
    lv_obj_set_height(chip, 44);
    lv_obj_set_style_pad_column(chip, 8, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(chip, LV_ALIGN_CENTER, 0, 124);
    act_batt = lv_obj_get_child(chip, 0);
    ui_icon(chip, &ic_bolt_28, C_DIM);
    lv_obj_move_to_index(act_batt, 1);

    bus_watch(tile, activity_event, NULL);
    activity_refresh();
}

/* ---- timer ---- */

static lv_obj_t *tt_time, *tt_presets;

static void timer_tile_refresh(void)
{
    char buf[28];
    bool sw = false;
    bool running = timer_ongoing(buf, sizeof(buf), &sw) && !sw;
    lv_label_set_text(tt_time, running ? buf : "Timer");
    lv_obj_set_style_text_font(tt_time, running ? &font_num_big : F36, 0);
    if (running) lv_obj_add_flag(tt_presets, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(tt_presets, LV_OBJ_FLAG_HIDDEN);
}

static void timer_tile_start(lv_event_t *e)
{
    timer_start((int)(intptr_t)lv_event_get_user_data(e));
    timer_tile_refresh();
}

static void timer_tile_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_SECOND) timer_tile_refresh();
}

static void timer_tile(lv_obj_t *tile)
{
    lv_color_t c = lv_color_hex(0xFF7A45);
    lv_obj_align(ui_icon(tile, &ic_hourglass_48, c), LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_t *box = ui_box(tile, 360, 110);
    lv_obj_add_event_cb(box, tile_open, LV_EVENT_SHORT_CLICKED, "timer");
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -34);
    tt_time = ui_label(box, "", F36, C_TEXT);
    lv_obj_center(tt_time);

    /* One tap starts a common countdown. */
    tt_presets = ui_box(tile, 330, 84);
    lv_obj_align(tt_presets, LV_ALIGN_CENTER, 0, 82);
    static const int mins[3] = {1, 5, 10};
    for (int i = 0; i < 3; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%d", mins[i]);
        lv_obj_t *b = ui_round_btn(tt_presets, 84, ui_tint(c, 26), NULL, c, timer_tile_start, (void *)(intptr_t)(mins[i] * 60));
        lv_obj_align(b, LV_ALIGN_CENTER, (i - 1) * 104, 0);
        lv_obj_align(ui_label(b, label, F28, c), LV_ALIGN_CENTER, 0, -8);
        lv_obj_align(ui_label(b, "min", F14, c), LV_ALIGN_CENTER, 0, 18);
    }
    bus_watch(tile, timer_tile_event, NULL);
    timer_tile_refresh();
}

/* ---- alarm ---- */

static lv_obj_t *at_time, *at_toggle;

static void alarm_tile_refresh(void)
{
    char buf[16];
    bool on = false;
    alarm_get(buf, sizeof(buf), &on);
    lv_label_set_text(at_time, buf);
    lv_obj_set_style_text_color(at_time, on ? C_TEXT : C_FAINT, 0);
    lv_obj_set_style_bg_color(at_toggle, on ? lv_color_hex(0xFDD663) : C_SURF2, 0);
    lv_obj_t *l = lv_obj_get_child(at_toggle, 0);
    lv_label_set_text(l, on ? "On" : "Off");
    lv_obj_set_style_text_color(l, on ? lv_color_black() : C_TEXT, 0);
}

static void alarm_tile_toggle(lv_event_t *e)
{
    LV_UNUSED(e);
    char buf[16];
    bool on = false;
    alarm_get(buf, sizeof(buf), &on);
    alarm_set_on(!on);
    alarm_tile_refresh();
}

static void alarm_tile_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_DISPLAY || e == EVT_SETTINGS) alarm_tile_refresh();
}

static void alarm_tile(lv_obj_t *tile)
{
    lv_obj_align(ui_icon(tile, &ic_alarm_48, lv_color_hex(0xFDD663)), LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_t *box = ui_box(tile, 360, 110);
    lv_obj_add_event_cb(box, tile_open, LV_EVENT_SHORT_CLICKED, "alarm");
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -30);
    at_time = ui_label(box, "", &font_num_big, C_TEXT);
    lv_obj_center(at_time);
    at_toggle = ui_pill_btn(tile, "Off", C_SURF2, C_TEXT, alarm_tile_toggle, NULL);
    lv_obj_align(at_toggle, LV_ALIGN_CENTER, 0, 90);
    bus_watch(tile, alarm_tile_event, NULL);
    alarm_tile_refresh();
}

/* ---- pet ---- */

static lv_obj_t *pt_mood;

static void pet_tile_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_DISPLAY || e == EVT_MINUTE) lv_label_set_text(pt_mood, pet_command("status"));
}

static void pet_tile(lv_obj_t *tile)
{
    lv_color_t c = lv_color_hex(0x7CE0D6);
    lv_obj_t *badge = ui_round_btn(tile, 120, ui_tint(c, 24), &ic_paw_48, c, tile_open, "pet");
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -62);
    pt_mood = ui_label(tile, pet_command("status"), F20, C_TEXT);
    lv_obj_set_width(pt_mood, 300);
    lv_obj_set_style_text_align(pt_mood, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(pt_mood, LV_ALIGN_CENTER, 0, 50);
    lv_obj_t *open = ui_pill_btn(tile, "Visit", C_SURF, C_DIM, tile_open, "pet");
    lv_obj_align(open, LV_ALIGN_BOTTOM_MID, 0, -44);
    bus_watch(tile, pet_tile_event, NULL);
}

void tile_create(lv_obj_t *tile, int kind)
{
    switch (kind) {
    case TILE_MUSIC: music_tile(tile); break;
    case TILE_ACTIVITY: activity_tile(tile); break;
    case TILE_WEATHER: weather_tile_create(tile); break;
    case TILE_TIMER: timer_tile(tile); break;
    case TILE_ALARM: alarm_tile(tile); break;
    case TILE_PET: pet_tile(tile); break;
    case TILE_AGENDA: agenda_tile_create(tile); break;
    default: break;
    }
}
