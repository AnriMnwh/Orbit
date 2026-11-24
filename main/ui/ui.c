#include "ui.h"

/* Soft tonal colours in the spirit of Material You. */
static const uint32_t ACCENTS[ACCENT_COUNT] = {
    0xA8C7FA, /* sky    */
    0xA8DAB5, /* mint   */
    0xFFB1C8, /* rose   */
    0xD0BCFF, /* violet */
    0xFFB68C, /* peach  */
    0xFDD663, /* sun    */
};

lv_color_t ui_accent_at(int index)
{
    if (index < 0 || index >= ACCENT_COUNT) index = 0;
    return lv_color_hex(ACCENTS[index]);
}

lv_color_t ui_accent(void)
{
    return ui_accent_at(CFG.accent);
}

lv_color_t ui_tint(lv_color_t c, int percent)
{
    return lv_color_mix(c, lv_color_black(), (uint8_t)(percent * 255 / 100));
}

static void strip(lv_obj_t *o)
{
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    strip(s);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s, C_TEXT, 0);
    lv_obj_set_style_text_font(s, F20, 0);
    return s;
}

lv_obj_t *ui_box(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    strip(o);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *ui_label_fit(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int width)
{
    lv_obj_t *l = ui_label(parent, text, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, width);
    lv_obj_set_height(l, lv_font_get_line_height(font));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

void ui_icon_set(lv_obj_t *icon, const lv_image_dsc_t *src, lv_color_t color)
{
    if (src) lv_image_set_src(icon, src);
    lv_obj_set_style_image_recolor(icon, color, 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
}

lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *src, lv_color_t color)
{
    lv_obj_t *i = lv_image_create(parent);
    ui_icon_set(i, src, color);
    return i;
}

lv_obj_t *ui_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = ui_label(parent, text, F16, C_DIM);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 34);
    return l;
}

lv_obj_t *ui_round_btn(lv_obj_t *parent, int size, lv_color_t bg, const lv_image_dsc_t *icon, lv_color_t fg,
                       lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_box(parent, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), bg, 56), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    /* Fingers are bigger than buttons: accept touches a little outside. */
    lv_obj_set_ext_click_area(b, 10);
    if (icon) {
        lv_obj_t *i = ui_icon(b, icon, fg);
        lv_obj_center(i);
    }
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_btn_icon(lv_obj_t *btn)
{
    return lv_obj_get_child(btn, 0);
}

lv_obj_t *ui_pill_btn(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_box(parent, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(lv_color_white(), bg, 56), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, 28, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_t *l = ui_label(b, text, F20, fg);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_arc(lv_obj_t *parent, int diameter, int width, lv_color_t track, lv_color_t color, int start, int sweep)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, diameter, diameter);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, (int32_t)start);
    lv_arc_set_bg_angles(a, 0, (lv_value_precise_t)sweep);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, track, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

void ui_arc_set(lv_obj_t *arc, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_arc_set_value(arc, percent);
    /* A zero-length rounded arc still draws its end caps as a dot. */
    lv_obj_set_style_arc_opa(arc, percent == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_INDICATOR);
}

/* -------------------------------------------------------------- time text */

static void time_text_tick(lv_timer_t *t)
{
    struct tm now;
    char buf[16];
    svc_localtime(&now);
    fmt_clock(buf, sizeof(buf), &now, false);
    lv_label_set_text(lv_timer_get_user_data(t), buf);
}

/* At this size the bezel's curve over five characters is under a pixel, so a
 * straight label reads the same as curved text and costs far less to draw.
 * The black plate lets it sit cleanly on top of edge arcs. */
lv_obj_t *ui_time_text(lv_obj_t *screen)
{
    lv_obj_t *l = ui_label(screen, "", F16, C_TEXT);
    lv_obj_set_style_bg_color(l, C_BG, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(l, 10, 0);
    lv_obj_set_style_pad_ver(l, 2, 0);
    lv_obj_set_style_radius(l, 10, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);
    lv_timer_t *t = ui_timer(l, time_text_tick, 5000, l);
    time_text_tick(t);
    return l;
}

/* ------------------------------------------------------------------- list */

#define BAR_SPAN 36  /* degrees of the right edge the scroll indicator covers */
#define BAR_THUMB 12

/* One timer per list, parked until scrolling stops. */
static void list_bar_hide(lv_timer_t *t)
{
    lv_obj_add_flag(lv_timer_get_user_data(t), LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

static void list_layout(lv_obj_t *list, bool show_bar)
{
    lv_obj_t *bar = lv_obj_get_user_data(list);
    lv_area_t la;
    lv_obj_get_coords(list, &la);
    int mid = (la.y1 + la.y2) / 2;
    uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(list, (int32_t)i);
        lv_area_t ra;
        lv_obj_get_coords(row, &ra);
        if (ra.y2 < la.y1 - 40 || ra.y1 > la.y2 + 40) continue;
        int d = LV_ABS((ra.y1 + ra.y2) / 2 - mid);
        if (d > SCR_C) d = SCR_C;
        int k = d * d * 100 / (SCR_C * SCR_C); /* 0 in the middle, 100 at the rim */
        lv_obj_set_style_text_opa(row, (lv_opa_t)(255 - k * 170 / 100), 0);
    }
    if (!bar) return;
    int above = lv_obj_get_scroll_top(list), below = lv_obj_get_scroll_bottom(list);
    if (above + below <= 4) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int at = above < 0 ? 0 : above * (BAR_SPAN - BAR_THUMB) / (above + below);
    if (at > BAR_SPAN - BAR_THUMB) at = BAR_SPAN - BAR_THUMB;
    lv_arc_set_angles(bar, (lv_value_precise_t)at, (lv_value_precise_t)(at + BAR_THUMB));
    if (show_bar) lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

static void list_scrolled(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_target_obj(e);
    lv_obj_t *bar = lv_obj_get_user_data(list);
    if (lv_event_get_code(e) == LV_EVENT_SCROLL) {
        list_layout(list, true);
    } else if (bar) {
        /* Scrolling stopped: let the indicator linger briefly, then hide it. */
        lv_timer_t *t = lv_obj_get_user_data(bar);
        lv_timer_reset(t);
        lv_timer_resume(t);
    }
}

lv_obj_t *ui_list(lv_obj_t *parent)
{
    lv_obj_t *list = ui_box(parent, SCR, SCR);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(list, 84, 0);
    lv_obj_set_style_pad_bottom(list, 130, 0);
    lv_obj_set_style_pad_row(list, 10, 0);

    lv_obj_t *bar = ui_arc(parent, 452, 6, C_SURF2, C_TEXT, 360 - BAR_SPAN / 2, BAR_SPAN);
    lv_obj_center(bar);
    lv_obj_set_style_arc_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t *hide = ui_timer(bar, list_bar_hide, 900, bar);
    lv_timer_pause(hide);
    lv_obj_set_user_data(bar, hide);
    lv_obj_set_user_data(list, bar);
    lv_obj_add_event_cb(list, list_scrolled, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(list, list_scrolled, LV_EVENT_SCROLL_END, NULL);
    return list;
}

void ui_list_refresh(lv_obj_t *list)
{
    lv_obj_update_layout(list);
    list_layout(list, false);
}

static void timer_owner_deleted(lv_event_t *e)
{
    lv_timer_delete(lv_event_get_user_data(e));
}

lv_timer_t *ui_timer(lv_obj_t *owner, lv_timer_cb_t cb, uint32_t period_ms, void *user)
{
    lv_timer_t *t = lv_timer_create(cb, period_ms, user);
    lv_obj_add_event_cb(owner, timer_owner_deleted, LV_EVENT_DELETE, t);
    return t;
}
