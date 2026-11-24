/* Music, Keys (macro pad) and Phone. */
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

/* ------------------------------------------------------------------ music */

#define MUSIC_COLOR 0xFF6B9A

static lv_obj_t *m_arc, *m_title, *m_artist, *m_play, *m_time, *m_src;
static lv_obj_t *m_art, *m_shuffle, *m_repeat, *m_like;

static void show_if(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void toggle_look(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_color(btn, on ? lv_color_hex(0xFF6B9A) : C_SURF, 0);
    ui_icon_set(ui_btn_icon(btn), NULL, on ? lv_color_black() : C_TEXT);
}

static int music_position(void)
{
    int p = S.media.position_s;
    if (S.media.playing) p += (int)((svc_millis() - S.media.stamp_ms) / 1000);
    if (S.media.duration_s && p > S.media.duration_s) p = S.media.duration_s;
    return p;
}

static void music_tick(void)
{
    bool have = S.media.valid && S.media.title[0];
    char buf[40];
    if (have && S.media.duration_s > 0) {
        int p = music_position();
        ui_arc_set(m_arc, p * 100 / S.media.duration_s);
        snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d", p / 60, p % 60, S.media.duration_s / 60, S.media.duration_s % 60);
    } else {
        ui_arc_set(m_arc, 0);
        buf[0] = 0;
    }
    ui_label_set(m_time, buf);
}

static void music_paint(void)
{
    bool have = S.media.valid && S.media.title[0];
    lv_label_set_text(m_title, have ? S.media.title : "Nothing playing");
    lv_label_set_text(m_artist, have ? S.media.artist : "");
    ui_icon_set(ui_btn_icon(m_play), S.media.playing ? &ic_pause_48 : &ic_play_48, lv_color_black());
    music_tick();

    char buf[40];

    if (S.link.keys_mode) snprintf(buf, sizeof(buf), "Controlling your computer");
    else if (S.link.status != LINK_CONNECTED) snprintf(buf, sizeof(buf), "Phone not connected");
    else if (S.media.volume >= 0 && S.media.app[0]) snprintf(buf, sizeof(buf), "%.16s  " LV_SYMBOL_BULLET "  %d%%", S.media.app, S.media.volume);
    else if (S.media.volume >= 0) snprintf(buf, sizeof(buf), "Volume %d%%", S.media.volume);
    else buf[0] = 0;
    lv_label_set_text(m_src, buf);

    /* With the Orbit phone app: the track's picture behind everything, and
     * whichever extra controls the current player accepts. */
    const lv_image_dsc_t *art = S.media.has_art ? svc_album_art() : NULL;
    if (art) lv_image_set_src(m_art, art);
    show_if(m_art, art != NULL);
    show_if(m_shuffle, S.media.can_shuffle);
    show_if(m_repeat, S.media.can_repeat);
    show_if(m_like, S.media.can_like);
    toggle_look(m_shuffle, S.media.shuffle);
    toggle_look(m_repeat, S.media.repeat);
}

/* Holding the skip buttons scrubs within the track instead. */
static void music_scrub(lv_event_t *e)
{
    if (!S.media.can_seek) return;
    svc_media_seek(music_position() + (int)(intptr_t)lv_event_get_user_data(e));
    shell_toast((intptr_t)lv_event_get_user_data(e) > 0 ? "+30 s" : "-10 s");
}

static void music_cmd(lv_event_t *e)
{
    svc_media((media_cmd_t)(uintptr_t)lv_event_get_user_data(e));
}

static void music_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_SECOND) music_tick();
    else if (e == EVT_MEDIA || e == EVT_LINK) music_paint();
}

static void music_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(MUSIC_COLOR);
    m_art = lv_image_create(scr);
    lv_obj_center(m_art);
    m_arc = ui_arc(scr, 442, 8, C_SURF, c, 270, 360);
    lv_obj_center(m_arc);

    m_title = ui_label_fit(scr, "", F28, C_TEXT, 320);
    lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 72);
    m_artist = ui_label_fit(scr, "", F20, C_DIM, 300);
    lv_obj_align(m_artist, LV_ALIGN_TOP_MID, 0, 110);
    m_time = ui_label(scr, "", F16, C_FAINT);
    lv_obj_align(m_time, LV_ALIGN_TOP_MID, 0, 142);

    lv_obj_t *prev = ui_round_btn(scr, 80, C_SURF2, &ic_prev_48, C_TEXT, music_cmd, (void *)MEDIA_PREV);
    lv_obj_align(prev, LV_ALIGN_CENTER, -120, 12);
    m_play = ui_round_btn(scr, 116, c, &ic_play_48, lv_color_black(), music_cmd, (void *)MEDIA_TOGGLE);
    lv_obj_align(m_play, LV_ALIGN_CENTER, 0, 12);
    lv_obj_t *next = ui_round_btn(scr, 80, C_SURF2, &ic_next_48, C_TEXT, music_cmd, (void *)MEDIA_NEXT);
    lv_obj_align(next, LV_ALIGN_CENTER, 120, 12);

    lv_obj_add_event_cb(prev, music_scrub, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)-10);
    lv_obj_add_event_cb(next, music_scrub, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)30);

    /* One row: volume in the middle, the optional extras around it. */
    m_shuffle = ui_round_btn(scr, 54, C_SURF, &ic_shuffle_28, C_TEXT, music_cmd, (void *)MEDIA_SHUFFLE);
    lv_obj_align(m_shuffle, LV_ALIGN_CENTER, -128, 114);
    lv_obj_t *down = ui_round_btn(scr, 60, C_SURF, &ic_vol_down_28, C_TEXT, music_cmd, (void *)MEDIA_VOL_DOWN);
    lv_obj_align(down, LV_ALIGN_CENTER, -64, 124);
    m_like = ui_round_btn(scr, 54, C_SURF, &ic_heart_28, C_TEXT, music_cmd, (void *)MEDIA_LIKE);
    lv_obj_align(m_like, LV_ALIGN_CENTER, 0, 128);
    lv_obj_t *up = ui_round_btn(scr, 60, C_SURF, &ic_vol_up_28, C_TEXT, music_cmd, (void *)MEDIA_VOL_UP);
    lv_obj_align(up, LV_ALIGN_CENTER, 64, 124);
    m_repeat = ui_round_btn(scr, 54, C_SURF, &ic_repeat_28, C_TEXT, music_cmd, (void *)MEDIA_REPEAT);
    lv_obj_align(m_repeat, LV_ALIGN_CENTER, 128, 114);

    m_src = ui_label(scr, "", F16, C_FAINT);
    lv_obj_align(m_src, LV_ALIGN_BOTTOM_MID, 0, -34);

    bus_watch(scr, music_event, NULL);
    music_paint();
}

const app_t app_music = {
    .id = "music", .name = "Music", .icon = &ic_music_48, .color = MUSIC_COLOR, .create = music_create,
};

/* ------------------------------------------------------------------- keys */

#define KEYS_COLOR 0x58B6FF
#define KEYS_PER_PAGE 7

static lv_obj_t *k_status;
static bool k_enabled_here; /* this app switched keyboard mode on, so it switches it off again */

static const lv_image_dsc_t *macro_icon(const macro_t *m)
{
    if (m->type != MACRO_MEDIA) return NULL;
    switch (m->usage) {
    case 0x00CD: return &ic_play_48;
    case 0x00B5: return &ic_next_48;
    case 0x00B6: return &ic_prev_48;
    case 0x00E9: return &ic_vol_up_48;
    case 0x00EA: return &ic_vol_down_48;
    default: return NULL;
    }
}

static void key_pressed(lv_event_t *e)
{
    macro_fire((int)(intptr_t)lv_event_get_user_data(e));
    if (CFG.sound) svc_sound(SND_TAP);
}

static void keys_paint_status(void)
{
    if (!k_status) return;
    if (S.link.keys_mode && S.link.status == LINK_CONNECTED) {
        lv_label_set_text(k_status, S.link.name[0] ? S.link.name : "Connected");
        lv_obj_set_style_text_color(k_status, lv_color_hex(KEYS_COLOR), 0);
    } else {
        lv_label_set_text(k_status, "Pair \"Orbit Keys\" on your computer");
        lv_obj_set_style_text_color(k_status, C_DIM, 0);
    }
}

static void keys_page(lv_obj_t *parent, int first)
{
    lv_obj_t *pg = ui_box(parent, SCR, SCR);
    lv_color_t c = lv_color_hex(KEYS_COLOR);
    /* One key in the middle, six around it: the layout a round screen wants. */
    static const int16_t px[KEYS_PER_PAGE] = {0, 0, 121, 121, 0, -121, -121};
    static const int16_t py[KEYS_PER_PAGE] = {0, -140, -70, 70, 140, 70, -70};
    for (int i = 0; i < KEYS_PER_PAGE; i++) {
        int idx = first + i;
        if (idx >= MACRO_MAX || !MACROS[idx].label[0]) continue;
        const macro_t *m = &MACROS[idx];
        bool centre = i == 0;
        lv_obj_t *b = ui_round_btn(pg, 124, centre ? c : C_SURF, NULL, C_TEXT, NULL, NULL);
        lv_obj_add_event_cb(b, key_pressed, LV_EVENT_PRESSED, (void *)(intptr_t)idx);
        lv_obj_set_ext_click_area(b, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, px[i], py[i]);
        lv_color_t fg = centre ? lv_color_black() : C_TEXT;
        const lv_image_dsc_t *icon = macro_icon(m);
        if (icon) {
            lv_obj_align(ui_icon(b, icon, fg), LV_ALIGN_CENTER, 0, -12);
            lv_obj_align(ui_label(b, m->label, F16, centre ? lv_color_black() : C_DIM), LV_ALIGN_CENTER, 0, 30);
        } else {
            lv_obj_center(ui_label(b, m->label, F24, fg));
        }
    }
}

static void keys_build(lv_obj_t *pager)
{
    lv_obj_clean(pager);
    for (int first = 0; first < MACRO_MAX; first += KEYS_PER_PAGE) keys_page(pager, first);
}

static void keys_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    if (e == EVT_LINK) keys_paint_status();
    if (e == EVT_MACROS) keys_build(user);
}

static void keys_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    k_status = NULL;
    if (k_enabled_here) svc_link_keys_mode(false);
    k_enabled_here = false;
}

static void keys_create(lv_obj_t *scr)
{
    lv_obj_t *pager = ui_box(scr, SCR, SCR);
    lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(pager, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(pager, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(pager, LV_OBJ_FLAG_SCROLL_ONE);
    keys_build(pager);

    k_status = ui_label(scr, "", F14, C_DIM);
    lv_obj_align(k_status, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(k_status, C_BG, 0);
    lv_obj_set_style_bg_opa(k_status, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(k_status, 10, 0);
    lv_obj_set_style_radius(k_status, 10, 0);

    lv_obj_add_event_cb(scr, keys_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, keys_event, pager);

    k_enabled_here = !S.link.keys_mode;
    if (k_enabled_here) svc_link_keys_mode(true);
    keys_paint_status();
}

const app_t app_keys = {
    .id = "keys", .name = "Keys", .icon = &ic_keys_48, .color = KEYS_COLOR, .create = keys_create,
    .keep_awake = true, .no_clock = true,
};

/* ------------------------------------------------------------------ phone */

#define PHONE_COLOR 0x4ADE9B

static lv_obj_t *p_state, *p_hint, *p_find, *p_find_label;
static bool p_ringing;

static void phone_paint(void)
{
    static const char *const kinds[] = {"", "iPhone", "Android", "Keyboard"};
    bool up = S.link.status == LINK_CONNECTED && !S.link.keys_mode;
    char buf[64];
    if (up) snprintf(buf, sizeof(buf), "%s", S.link.name[0] ? S.link.name : kinds[S.link.peer]);
    else snprintf(buf, sizeof(buf), "%s", S.link.keys_mode ? "Keyboard mode" : "Not connected");
    lv_label_set_text(p_state, buf);
    lv_obj_set_style_text_color(p_state, up ? C_TEXT : C_DIM, 0);

    if (up && S.phone.known) {
        /* The Orbit phone app also reports the phone's own battery. */
        snprintf(buf, sizeof(buf), "Phone battery %d%%%s\n%d notification%s", S.phone.percent,
                 S.phone.charging ? ", charging" : "", S.notif_count, S.notif_count == 1 ? "" : "s");
    } else if (up) snprintf(buf, sizeof(buf), "%d notification%s", S.notif_count, S.notif_count == 1 ? "" : "s");
    else if (S.link.keys_mode) snprintf(buf, sizeof(buf), "Turn off Keys to reconnect your phone");
    else snprintf(buf, sizeof(buf), "iPhone: Bluetooth settings > Orbit\nAndroid: add in Gadgetbridge");
    lv_label_set_text(p_hint, buf);

    bool can_find = up && S.link.peer == PEER_ANDROID;
    lv_obj_set_style_bg_color(p_find, p_ringing ? lv_color_hex(PHONE_COLOR) : C_SURF2, 0);
    ui_icon_set(ui_btn_icon(p_find), NULL, p_ringing ? lv_color_black() : (can_find ? C_TEXT : C_FAINT));
    lv_label_set_text(p_find_label, p_ringing ? "Ringing" : "Find phone");
}

static void phone_find(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.link.status != LINK_CONNECTED || S.link.peer != PEER_ANDROID) {
        shell_toast(S.link.peer == PEER_IPHONE ? "Not available on iPhone" : "Phone not connected");
        return;
    }
    p_ringing = !p_ringing;
    svc_find_phone(p_ringing);
    phone_paint();
}

static void phone_notifs(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_home();
    shell_show_tile(1, 2);
}

static void phone_dnd(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.dnd = !CFG.dnd;
    settings_changed();
    shell_toast(CFG.dnd ? "Do not disturb on" : "Do not disturb off");
}

static void phone_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_LINK || e == EVT_NOTIF) phone_paint();
}

static void phone_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (p_ringing) svc_find_phone(false);
    p_ringing = false;
}

static lv_obj_t *phone_action(lv_obj_t *scr, const lv_image_dsc_t *icon, const char *text, int x, lv_event_cb_t cb,
                              lv_obj_t **label)
{
    lv_obj_t *b = ui_round_btn(scr, 88, C_SURF2, icon, C_TEXT, cb, NULL);
    lv_obj_align(b, LV_ALIGN_CENTER, x, 62);
    lv_obj_t *l = ui_label(scr, text, F16, C_DIM);
    lv_obj_align(l, LV_ALIGN_CENTER, x, 124);
    if (label) *label = l;
    return b;
}

static void phone_create(lv_obj_t *scr)
{
    lv_obj_align(ui_icon(scr, &ic_phone_48, lv_color_hex(PHONE_COLOR)), LV_ALIGN_TOP_MID, 0, 46);
    p_state = ui_label_fit(scr, "", F28, C_TEXT, 320);
    lv_obj_align(p_state, LV_ALIGN_TOP_MID, 0, 106);
    p_hint = ui_label(scr, "", F16, C_DIM);
    lv_obj_set_width(p_hint, 330);
    lv_obj_set_style_text_align(p_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(p_hint, LV_ALIGN_TOP_MID, 0, 148);

    p_find = phone_action(scr, &ic_target_48, "Find phone", -112, phone_find, &p_find_label);
    phone_action(scr, &ic_bell_48, "Alerts", 0, phone_notifs, NULL);
    phone_action(scr, &ic_moon_48, "Quiet", 112, phone_dnd, NULL);

    lv_obj_add_event_cb(scr, phone_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, phone_event, NULL);
    phone_paint();
}

const app_t app_phone = {
    .id = "phone", .name = "Phone", .icon = &ic_phone_48, .color = PHONE_COLOR, .create = phone_create,
};
