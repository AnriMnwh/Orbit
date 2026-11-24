/* Watch faces (Digital, Analog, Modular, Photo, Minimal, Custom), their
 * dimmed always-on versions, the Apple Watch-style My Faces switcher +
 * Gallery, and the on-watch Edit mode (Colour, Style, Complications).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

static const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static const char *const FACE_NAMES[FACE_COUNT] = {"Digital", "Analog", "Modular", "Photo", "Minimal", "Custom"};
/* Minimal and Custom have no style variants. */
static const char *const STYLE_NAMES[FACE_COUNT][3] = {
    {"Stacked", "Compact", "Minimal"},
    {"Classic", "Minimal", "Sport"},
    {"Agenda", "Weather", "Pet"},
    {"Bottom", "Top", "Corner"},
};
static const char *const COMP_NAMES[COMP_COUNT] = {"Battery", "Steps", "Weather", "Day", "Pet", "Agenda", "None"};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *hours, *minutes, *time, *date;
    lv_obj_t *comp_box[2], *comp_arc[2], *comp_text[2], *comp_icon[2];
    lv_obj_t *mod_title, *mod_sub;
    lv_obj_t *status, *status_icon, *status_lbl;
    lv_obj_t *analog, *charge_arc, *charge_text;
    uint8_t face;
    uint8_t style;
    bool ambient;
    bool edit_comps;
    int photo_index;
    const app_t *last_ongoing_app;
    bool has_ongoing;
} face_t;

static face_t live;
static uint8_t built_accent, built_24h, built_comp[2], built_style, built_custom;
static uint32_t built_custom_gen;
static bool previewing;

static void fmt_date(char *buf, size_t len, const struct tm *t)
{
    snprintf(buf, len, "%s %d %s", DAYS[t->tm_wday % 7], t->tm_mday, MONTHS[t->tm_mon % 12]);
}

static int visible_face_count(void)
{
    int n = 0;
    while (n < FACE_COUNT && CFG.face_order[n] < FACE_COUNT) n++;
    return n > 0 ? n : 1;
}

/* ------------------------------------------------- status row and ongoing */

static void status_item(lv_obj_t *row, const lv_image_dsc_t *icon, lv_color_t color, const char *text)
{
    lv_obj_t *item = ui_box(row, LV_SIZE_CONTENT, 32);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 6, 0);
    if (icon) ui_icon(item, icon, color);
    if (text) ui_label(item, text, F20, color);
}

static void ongoing_open(lv_event_t *e)
{
    LV_UNUSED(e);
    ongoing_t o;
    if (shell_ongoing(&o)) shell_open(o.app);
}

/* Bottom of the face: a chip for whatever is still running (timer, music,
 * camera), otherwise small status glyphs. Updates text in-place when the
 * ongoing app hasn't changed so EVT_SECOND never reallocates widgets. */
static void status_refresh(face_t *f)
{
    if (!f->status) return;
    ongoing_t o;
    bool has = shell_ongoing(&o);
    if (has && f->has_ongoing && f->last_ongoing_app == o.app && f->status_lbl) {
        lv_label_set_text(f->status_lbl, o.text);
        return;
    }

    lv_obj_clean(f->status);
    f->status_icon = NULL;
    f->status_lbl = NULL;
    f->has_ongoing = has;
    f->last_ongoing_app = has ? o.app : NULL;
    lv_obj_set_style_bg_opa(f->status, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(f->status, LV_OBJ_FLAG_CLICKABLE);

    if (has) {
        lv_obj_set_style_bg_color(f->status, C_SURF, 0);
        lv_obj_set_style_bg_opa(f->status, LV_OPA_COVER, 0);
        lv_obj_add_flag(f->status, LV_OBJ_FLAG_CLICKABLE);
        f->status_icon = ui_icon(f->status, o.icon, ui_accent());
        f->status_lbl = ui_label(f->status, o.text, F16, C_TEXT);
        lv_label_set_long_mode(f->status_lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(f->status_lbl, 150, 0);
        return;
    }
    char buf[16];
    if (S.notif_count > 0) {
        snprintf(buf, sizeof(buf), "%d", S.notif_count);
        status_item(f->status, &ic_bell_28, ui_accent(), buf);
    }
    if (S.link.keys_mode) status_item(f->status, &ic_keys_28, C_DIM, NULL);
    else if (S.link.status != LINK_CONNECTED) status_item(f->status, &ic_bluetooth_28, C_FAINT, NULL);
    if (CFG.dnd) status_item(f->status, &ic_moon_28, C_DIM, NULL);
}

static lv_obj_t *status_row(lv_obj_t *parent)
{
    lv_obj_t *row = ui_box(parent, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_style_radius(row, 20, 0);
    lv_obj_add_event_cb(row, ongoing_open, LV_EVENT_SHORT_CLICKED, NULL);
    return row;
}

/* ------------------------------------------------------------ complications */

static const lv_image_dsc_t *comp_icon_for(int kind)
{
    switch (kind) {
    case COMP_BATTERY: return &ic_bolt_28;
    case COMP_STEPS: return &ic_steps_28;
    case COMP_WEATHER: return &ic_cloud_28;
    case COMP_PET: return &ic_paw_28;
    case COMP_AGENDA: return &ic_calendar_28;
    default: return &ic_clock_28;
    }
}

static const char *comp_app_id(int kind)
{
    switch (kind) {
    case COMP_BATTERY: return "settings";
    case COMP_STEPS: return "fitness";
    case COMP_WEATHER: return "weather";
    case COMP_DAY: return "calendar";
    case COMP_PET: return "pet";
    case COMP_AGENDA: return "agenda";
    default: return NULL;
    }
}

static void comp_tapped(lv_event_t *e)
{
    int kind = (int)(intptr_t)lv_event_get_user_data(e);
    const char *id = comp_app_id(kind);
    if (id) shell_open(app_find(id));
}

static void comp_build_color(face_t *f, int side, int kind, lv_align_t align, int x, int y, lv_color_t color)
{
    if (kind == COMP_NONE && !f->edit_comps) return;
    lv_obj_t *box = ui_box(f->root, 84, 84);
    f->comp_box[side] = box;
    lv_obj_align(box, align, x, y);

    if (f->edit_comps) {
        lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(box, 3, 0);
        lv_obj_set_style_border_color(box, ui_accent(), 0);
        lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    }

    if (kind == COMP_NONE) {
        f->comp_text[side] = ui_label(box, "+", F24, ui_accent());
        lv_obj_center(f->comp_text[side]);
        return;
    }

    if (!f->ambient && !previewing && !f->edit_comps) {
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(box, 10);
        lv_obj_add_event_cb(box, comp_tapped, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)kind);
    } else {
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    }

    f->comp_arc[side] = ui_arc(box, 80, 7, C_SURF2, color, 135, 270);
    lv_obj_center(f->comp_arc[side]);
    f->comp_text[side] = ui_label(box, "", F20, C_TEXT);
    lv_obj_align(f->comp_text[side], LV_ALIGN_CENTER, 0, -3);
    f->comp_icon[side] = ui_icon(box, comp_icon_for(kind), color);
    lv_image_set_scale(f->comp_icon[side], 150);
    lv_obj_align(f->comp_icon[side], LV_ALIGN_BOTTOM_MID, 0, 6);
}

static void comp_build_at(face_t *f, int side, int kind, lv_align_t align, int x, int y)
{
    comp_build_color(f, side, kind, align, x, y, side == 0 ? ui_accent() : C_TEXT);
}

static void comp_refresh(face_t *f, int side, int kind)
{
    if (!f->comp_arc[side]) return;
    char buf[16] = "";
    int percent = 0;
    struct tm t;
    switch (kind) {
    case COMP_BATTERY: {
        percent = S.batt.percent < 0 ? 0 : S.batt.percent;
        if (S.batt.percent < 0) snprintf(buf, sizeof(buf), "--");
        else snprintf(buf, sizeof(buf), "%d", S.batt.percent);
        lv_color_t c = S.batt.charging ? C_GOOD : (S.batt.percent >= 0 && S.batt.percent <= 15 ? C_BAD : ui_accent());
        lv_obj_set_style_arc_color(f->comp_arc[side], c, LV_PART_INDICATOR);
        break;
    }
    case COMP_STEPS:
        percent = CFG.step_goal ? (int)(S.steps * 100 / CFG.step_goal) : 0;
        if (S.steps >= 10000) snprintf(buf, sizeof(buf), "%uk", (unsigned)(S.steps / 1000));
        else if (S.steps >= 1000) snprintf(buf, sizeof(buf), "%u.%uk", (unsigned)(S.steps / 1000), (unsigned)(S.steps % 1000 / 100));
        else snprintf(buf, sizeof(buf), "%u", (unsigned)S.steps);
        break;
    case COMP_WEATHER:
        percent = 100;
        if (S.weather.valid) snprintf(buf, sizeof(buf), "%d\xC2\xB0", S.weather.temp_c);
        else snprintf(buf, sizeof(buf), "--");
        break;
    case COMP_PET: {
        int mood = 0;
        pet_summary(&percent, &mood);
        snprintf(buf, sizeof(buf), "%d%%", percent);
        break;
    }
    case COMP_AGENDA: {
        time_t now = svc_time();
        int upcoming = 0;
        time_t next_s = 0;
        for (int i = 0; i < S.agenda_count; i++) {
            if (S.agenda[i].end > now) {
                if (!upcoming) next_s = S.agenda[i].start;
                upcoming++;
            }
        }
        percent = upcoming > 0 ? LV_MIN(upcoming * 25, 100) : 0;
        if (upcoming > 0 && next_s > 0) {
            time_t local = next_s + (time_t)CFG.tz_minutes * 60;
            struct tm lt;
            gmtime_r(&local, &lt);
            snprintf(buf, sizeof(buf), "%d:%02d", CFG.clock_24h ? lt.tm_hour : (lt.tm_hour % 12 ? lt.tm_hour % 12 : 12), lt.tm_min);
        } else {
            snprintf(buf, sizeof(buf), "%d", upcoming);
        }
        break;
    }
    default: /* COMP_DAY */
        svc_localtime(&t);
        percent = (t.tm_hour * 60 + t.tm_min) * 100 / 1440;
        snprintf(buf, sizeof(buf), "%d", t.tm_mday);
        break;
    }
    ui_arc_set(f->comp_arc[side], percent);
    lv_label_set_text(f->comp_text[side], buf);
}

/* ------------------------------------------------------------ analog dial */

static struct {
    bool ready;
    lv_point_precise_t p1[60], p2[60];
} tick_lut;

static void init_tick_lut(void)
{
    if (tick_lut.ready) return;
    tick_lut.ready = true;
    for (int i = 0; i < 60; i++) {
        float a = (i * 6.0f - 90.0f) * 3.14159265f / 180.0f;
        float ca = cosf(a), sa = sinf(a);
        float r0 = (i % 5 == 0) ? 196.0f : 208.0f;
        float r1 = 218.0f;
        tick_lut.p1[i].x = (lv_value_precise_t)(SCR_C + r0 * ca);
        tick_lut.p1[i].y = (lv_value_precise_t)(SCR_C + r0 * sa);
        tick_lut.p2[i].x = (lv_value_precise_t)(SCR_C + r1 * ca);
        tick_lut.p2[i].y = (lv_value_precise_t)(SCR_C + r1 * sa);
    }
}

static void polar(float deg, float r, lv_point_precise_t *p)
{
    float a = (deg - 90.0f) * 3.14159265f / 180.0f;
    p->x = (lv_value_precise_t)(SCR_C + r * cosf(a));
    p->y = (lv_value_precise_t)(SCR_C + r * sinf(a));
}

static void stroke(lv_layer_t *layer, const lv_area_t *origin, float deg, float r0, float r1, int width, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = width;
    d.round_start = 1;
    d.round_end = 1;
    polar(deg, r0, &d.p1);
    polar(deg, r1, &d.p2);
    d.p1.x += origin->x1, d.p1.y += origin->y1;
    d.p2.x += origin->x1, d.p2.y += origin->y1;
    lv_draw_line(layer, &d);
}

static void analog_draw(lv_event_t *e)
{
    init_tick_lut();
    lv_obj_t *obj = lv_event_get_target_obj(e);
    uintptr_t flags = (uintptr_t)lv_event_get_user_data(e);
    bool ambient = (flags & 1) != 0;
    uint8_t style = (uint8_t)((flags >> 1) & 3);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    struct tm t;
    svc_localtime(&t);
    lv_color_t acc = ui_accent();

    int step = (ambient || style == 1) ? 5 : 1;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.round_start = 1;
    d.round_end = 1;
    for (int i = 0; i < 60; i += step) {
        bool major = i % 5 == 0;
        d.color = major ? (ambient ? C_FAINT : (style == 2 && i == 0 ? acc : C_TEXT)) : C_FAINT;
        d.width = major ? (ambient ? 3 : 5) : 2;
        d.p1.x = tick_lut.p1[i].x + a.x1;
        d.p1.y = tick_lut.p1[i].y + a.y1;
        d.p2.x = tick_lut.p2[i].x + a.x1;
        d.p2.y = tick_lut.p2[i].y + a.y1;
        lv_draw_line(layer, &d);
    }

    float m = t.tm_min + (ambient ? 0 : t.tm_sec / 60.0f);
    float h = (t.tm_hour % 12) + m / 60.0f;
    stroke(layer, &a, h * 30.0f, 0, 104, ambient ? 6 : 13, ambient ? C_DIM : C_TEXT);
    stroke(layer, &a, m * 6.0f, 0, 164, ambient ? 4 : 9, ambient ? C_DIM : (style == 2 ? acc : C_TEXT));
    if (ambient) return;
    stroke(layer, &a, t.tm_sec * 6.0f, -34, 186, 3, acc);

    lv_draw_rect_dsc_t cap;
    lv_draw_rect_dsc_init(&cap);
    cap.radius = LV_RADIUS_CIRCLE;
    cap.bg_color = acc;
    lv_area_t c = {a.x1 + SCR_C - 9, a.y1 + SCR_C - 9, a.x1 + SCR_C + 9, a.y1 + SCR_C + 9};
    lv_draw_rect(layer, &cap, &c);
    cap.bg_color = lv_color_black();
    lv_area_t c2 = {a.x1 + SCR_C - 3, a.y1 + SCR_C - 3, a.x1 + SCR_C + 3, a.y1 + SCR_C + 3};
    lv_draw_rect(layer, &cap, &c2);
}

/* ---------------------------------------------------------------- building */

static void modular_refresh(face_t *f)
{
    if (!f->mod_title || !f->mod_sub) return;
    if (f->style == 1) {
        if (S.weather.valid) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d\xC2\xB0  %s", S.weather.temp_c, S.weather.text);
            lv_label_set_text(f->mod_title, buf);
            snprintf(buf, sizeof(buf), "High %d\xC2\xB0  Low %d\xC2\xB0", S.weather.hi_c, S.weather.lo_c);
            lv_label_set_text(f->mod_sub, buf);
        } else {
            lv_label_set_text(f->mod_title, "Weather");
            lv_label_set_text(f->mod_sub, "Connect phone for forecast");
        }
    } else if (f->style == 2) {
        static const char *const moods[] = {"Sleeping", "Happy", "Content", "Hungry", "Bored", "Sleepy", "Needs wash", "Sick!"};
        int pct = 0, mood = 0;
        pet_summary(&pct, &mood);
        char buf[32];
        snprintf(buf, sizeof(buf), "Pet: %s", moods[mood >= 0 && mood < 8 ? mood : 1]);
        lv_label_set_text(f->mod_title, buf);
        snprintf(buf, sizeof(buf), "Overall health %d%%", pct);
        lv_label_set_text(f->mod_sub, buf);
    } else {
        time_t now = svc_time();
        const agenda_item_t *next = NULL;
        for (int i = 0; i < S.agenda_count; i++) {
            if (S.agenda[i].end > now) {
                next = &S.agenda[i];
                break;
            }
        }
        if (next) {
            lv_label_set_text(f->mod_title, next->title);
            if (next->all_day) {
                lv_label_set_text(f->mod_sub, next->place[0] ? next->place : "All day");
            } else {
                time_t local = next->start + (time_t)CFG.tz_minutes * 60;
                struct tm lt;
                gmtime_r(&local, &lt);
                char buf[48], clk[16];
                fmt_clock(clk, sizeof(clk), &lt, false);
                snprintf(buf, sizeof(buf), "%s%s%s", clk, next->place[0] ? "  " : "", next->place);
                lv_label_set_text(f->mod_sub, buf);
            }
        } else {
            lv_label_set_text(f->mod_title, "No upcoming events");
            lv_label_set_text(f->mod_sub, S.agenda_known ? "Schedule clear" : "Connect phone for agenda");
        }
    }
}

static void face_time(face_t *f)
{
    struct tm t;
    svc_localtime(&t);
    char buf[24];

    if (f->hours) {
        int h = t.tm_hour;
        if (!CFG.clock_24h) h = h % 12 ? h % 12 : 12;
        snprintf(buf, sizeof(buf), CFG.clock_24h ? "%02d" : "%d", h);
        lv_label_set_text(f->hours, buf);
        snprintf(buf, sizeof(buf), "%02d", t.tm_min);
        lv_label_set_text(f->minutes, buf);
    }
    if (f->time) {
        fmt_clock(buf, sizeof(buf), &t, false);
        lv_label_set_text(f->time, buf);
    }
    if (f->date) {
        fmt_date(buf, sizeof(buf), &t);
        lv_label_set_text(f->date, buf);
    }
    if (f->analog) lv_obj_invalidate(f->analog);
    if (f->charge_arc) {
        ui_arc_set(f->charge_arc, S.batt.percent < 0 ? 0 : S.batt.percent);
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %d%%", S.batt.percent < 0 ? 0 : S.batt.percent);
        lv_label_set_text(f->charge_text, buf);
    }
}

static void face_refresh(face_t *f)
{
    face_time(f);
    comp_refresh(f, 0, CFG.comp_left);
    comp_refresh(f, 1, CFG.comp_right);
    modular_refresh(f);
    status_refresh(f);
}

static void build_digital(face_t *f)
{
    lv_color_t acc = ui_accent();
    f->date = ui_label(f->root, "", F20, C_DIM);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, f->style == 1 ? 56 : 50);

    if (f->style == 1) {
        f->time = ui_label(f->root, "", &font_num_hero_bold, f->ambient ? ui_tint(acc, 70) : acc);
        lv_obj_align(f->time, LV_ALIGN_CENTER, 0, -34);
        if (f->ambient) return;
        comp_build_at(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -76, 86);
        comp_build_at(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 76, 86);
    } else {
        f->hours = ui_label(f->root, "", f->ambient ? &font_num_hero_light : &font_num_hero_bold,
                            f->ambient ? ui_tint(acc, 70) : acc);
        lv_obj_align(f->hours, LV_ALIGN_CENTER, 0, -66);
        f->minutes = ui_label(f->root, "", &font_num_hero_light, f->ambient ? C_DIM : C_TEXT);
        lv_obj_align(f->minutes, LV_ALIGN_CENTER, 0, 58);
        if (f->ambient) return;
        if (f->style != 2) {
            comp_build_at(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -172, 0);
            comp_build_at(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 172, 0);
        }
    }
    if (previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -32);
}

static void build_analog(face_t *f)
{
    f->analog = ui_box(f->root, SCR, SCR);
    lv_obj_remove_flag(f->analog, LV_OBJ_FLAG_CLICKABLE);
    uintptr_t flags = (f->ambient ? 1u : 0u) | ((uintptr_t)(f->style & 3) << 1);
    lv_obj_add_event_cb(f->analog, analog_draw, LV_EVENT_DRAW_MAIN, (void *)flags);

    f->date = ui_label(f->root, "", F20, f->ambient ? C_FAINT : C_DIM);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 96);
    if (f->ambient) return;
    if (f->style == 0) {
        comp_build_at(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -124, 0);
        comp_build_at(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 124, 0);
    }
    if (previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_CENTER, 0, -96);
}

static void mod_card_tap(lv_event_t *e)
{
    uint8_t style = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    shell_open(app_find(style == 1 ? "weather" : (style == 2 ? "pet" : "agenda")));
}

static void build_modular(face_t *f)
{
    lv_color_t acc = ui_accent();
    f->date = ui_label(f->root, "", F16, f->ambient ? C_FAINT : C_DIM);
    lv_obj_set_style_text_letter_space(f->date, 2, 0);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 56);

    f->time = ui_label(f->root, "", &font_num_big, f->ambient ? C_DIM : acc);
    lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *card = ui_box(f->root, 336, 92);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, f->ambient ? C_BG : C_SURF, 0);
    lv_obj_set_style_bg_opa(card, f->ambient ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (f->ambient) {
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, C_SURF2, 0);
    }
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 4);
    if (!f->ambient && !previewing && !f->edit_comps) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, mod_card_tap, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)f->style);
    }

    f->mod_title = ui_label(card, "", F20, f->ambient ? C_DIM : C_TEXT);
    lv_label_set_long_mode(f->mod_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(f->mod_title, 300);
    lv_obj_align(f->mod_title, LV_ALIGN_TOP_LEFT, 18, 16);

    f->mod_sub = ui_label(card, "", F16, f->ambient ? C_FAINT : acc);
    lv_label_set_long_mode(f->mod_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(f->mod_sub, 300);
    lv_obj_align(f->mod_sub, LV_ALIGN_BOTTOM_LEFT, 18, -16);

    if (f->ambient) return;
    comp_build_at(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -76, 114);
    comp_build_at(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 76, 114);
    if (previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static void build_photo(face_t *f)
{
    int count = svc_photo_count();
    const lv_image_dsc_t *img = count > 0 ? svc_photo(f->photo_index % count) : NULL;
    if (img) {
        lv_obj_t *photo = lv_image_create(f->root);
        lv_image_set_src(photo, img);
        lv_obj_center(photo);
        if (f->style != 2) {
            lv_obj_t *shade = ui_box(f->root, SCR, 250);
            lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(shade, f->style == 1 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
            lv_obj_set_style_bg_grad_color(shade, lv_color_black(), 0);
            lv_obj_set_style_bg_grad_dir(shade, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_bg_opa(shade, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_main_opa(shade, f->style == 1 ? 220 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_grad_opa(shade, f->style == 1 ? LV_OPA_TRANSP : 220, 0);
        }
    } else {
        lv_obj_t *hint = ui_label(f->root, "Add photos from your phone\nSettings  >  Wi-Fi shows how", F20, C_DIM);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, -40);
    }
    f->time = ui_label(f->root, "", (f->ambient || f->style == 2) ? &font_num_mid : &font_num_big, C_TEXT);
    f->date = ui_label(f->root, "", F20, C_TEXT);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    if (f->style == 1) {
        lv_obj_align(f->time, LV_ALIGN_TOP_MID, 0, 52);
        lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 126);
    } else if (f->style == 2) {
        lv_obj_align(f->time, LV_ALIGN_BOTTOM_MID, 0, -68);
        lv_obj_align(f->date, LV_ALIGN_BOTTOM_MID, 0, -38);
    } else {
        lv_obj_align(f->time, LV_ALIGN_BOTTOM_MID, 0, f->ambient ? -92 : -84);
        lv_obj_align(f->date, LV_ALIGN_BOTTOM_MID, 0, -52);
    }
}

/* Bauhaus / Pixel Watch Minimal: 12 clean baton indices, slender precision
 * hands, cardinal accents, and a pill date window at 3 o'clock. */
static void minimal_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    uintptr_t flags = (uintptr_t)lv_event_get_user_data(e);
    bool ambient = (flags & 1) != 0;
    lv_color_t acc = (flags >> 8) ? ui_accent_at((int)((flags >> 8) - 1)) : ui_accent();
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    struct tm t;
    svc_localtime(&t);

    for (int i = 0; i < 12; i++) {
        bool card = i % 3 == 0;
        if (ambient && !card) continue;
        stroke(layer, &a, i * 30.0f, card ? 190 : 204, 218, card ? (ambient ? 3 : 6) : 3,
               card ? (ambient ? C_DIM : acc) : C_FAINT);
    }
    float m = t.tm_min + (ambient ? 0 : t.tm_sec / 60.0f);
    float h = (t.tm_hour % 12) + m / 60.0f;
    stroke(layer, &a, h * 30.0f, 0, 112, ambient ? 5 : 10, ambient ? C_DIM : C_TEXT);
    stroke(layer, &a, m * 6.0f, 0, 174, ambient ? 3 : 6, ambient ? C_DIM : C_TEXT);
    if (ambient) return;
    stroke(layer, &a, t.tm_sec * 6.0f, -28, 192, 2, acc);

    lv_draw_rect_dsc_t cap;
    lv_draw_rect_dsc_init(&cap);
    cap.radius = LV_RADIUS_CIRCLE;
    cap.bg_color = acc;
    lv_area_t c = {a.x1 + SCR_C - 7, a.y1 + SCR_C - 7, a.x1 + SCR_C + 7, a.y1 + SCR_C + 7};
    lv_draw_rect(layer, &cap, &c);
}

static void build_minimal(face_t *f)
{
    f->analog = ui_box(f->root, SCR, SCR);
    lv_obj_remove_flag(f->analog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(f->analog, minimal_draw, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)(f->ambient ? 1 : 0));

    if (!f->ambient) {
        f->date = ui_label(f->root, "", F16, C_TEXT);
        lv_obj_set_style_bg_color(f->date, C_SURF, 0);
        lv_obj_set_style_bg_opa(f->date, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(f->date, 10, 0);
        lv_obj_set_style_pad_ver(f->date, 4, 0);
        lv_obj_set_style_radius(f->date, 12, 0);
        lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 88);
    }
    if (f->ambient || previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_CENTER, 0, -96);
}

static void custom_cycle(lv_event_t *e)
{
    LV_UNUSED(e);
    if (CUSTOM_FACE_COUNT <= 1) return;
    CFG.custom_face = (uint8_t)((CFG.custom_face + 1) % CUSTOM_FACE_COUNT);
    settings_changed();
    shell_toast(CUSTOM_FACES[CFG.custom_face].name);
}

/* User-customisable face: configured from the setup page (http://orbit.local).
 * Tap to cycle through saved custom faces. */
static void build_custom(face_t *f)
{
    const custom_face_t *cf = &CUSTOM_FACES[CFG.custom_face < CUSTOM_FACE_COUNT ? CFG.custom_face : 0];
    lv_color_t acc = ui_accent_at(cf->accent);

    if (!f->ambient) {
        if (cf->photo_idx > 0 && svc_photo_count() > 0) {
            const lv_image_dsc_t *img = svc_photo((cf->photo_idx - 1) % svc_photo_count());
            if (img) {
                lv_obj_t *photo = lv_image_create(f->root);
                lv_image_set_src(photo, img);
                lv_obj_center(photo);
                lv_obj_t *veil = ui_box(f->root, SCR, SCR);
                lv_obj_remove_flag(veil, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_bg_color(veil, lv_color_black(), 0);
                lv_obj_set_style_bg_opa(veil, LV_OPA_40, 0);
            }
        } else if (cf->bg_hex != 0) {
            lv_obj_t *bg = ui_box(f->root, SCR, SCR);
            lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(bg, lv_color_hex(cf->bg_hex), 0);
            lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
        }
    }

    if (cf->style == CF_STYLE_ANALOG) {
        f->analog = ui_box(f->root, SCR, SCR);
        lv_obj_remove_flag(f->analog, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(f->analog, analog_draw, LV_EVENT_DRAW_MAIN, f->ambient ? (void *)1 : NULL);
        if (cf->show_date) {
            f->date = ui_label(f->root, "", F20, f->ambient ? C_FAINT : C_DIM);
            lv_obj_set_style_text_letter_space(f->date, 3, 0);
            lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 96);
        }
    } else if (cf->style == CF_STYLE_MINIMAL) {
        uintptr_t flags = (uintptr_t)(f->ambient ? 1 : 0) | ((uintptr_t)(cf->accent + 1) << 8);
        f->analog = ui_box(f->root, SCR, SCR);
        lv_obj_remove_flag(f->analog, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(f->analog, minimal_draw, LV_EVENT_DRAW_MAIN, (void *)flags);
        if (cf->show_date && !f->ambient) {
            f->date = ui_label(f->root, "", F16, C_TEXT);
            lv_obj_set_style_bg_color(f->date, C_SURF, 0);
            lv_obj_set_style_bg_opa(f->date, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(f->date, 10, 0);
            lv_obj_set_style_pad_ver(f->date, 4, 0);
            lv_obj_set_style_radius(f->date, 12, 0);
            lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 88);
        }
    } else if (cf->style == CF_STYLE_COMPACT) {
        if (cf->show_date) {
            f->date = ui_label(f->root, "", F20, f->ambient ? C_FAINT : acc);
            lv_obj_set_style_text_letter_space(f->date, 3, 0);
            lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 58);
        }
        f->time = ui_label(f->root, "", f->ambient ? &font_num_mid : &font_num_big, f->ambient ? C_DIM : C_TEXT);
        lv_obj_align(f->time, LV_ALIGN_CENTER, 0, cf->show_comps && !f->ambient ? -38 : 0);
        if (cf->show_comps && !f->ambient) {
            comp_build_color(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -74, 82, acc);
            comp_build_color(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 74, 82, C_TEXT);
        }
    } else {
        if (cf->show_date) {
            f->date = ui_label(f->root, "", F20, C_DIM);
            lv_obj_set_style_text_letter_space(f->date, 3, 0);
            lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 50);
        }
        f->hours = ui_label(f->root, "", f->ambient ? &font_num_hero_light : &font_num_hero_bold,
                            f->ambient ? ui_tint(acc, 70) : acc);
        lv_obj_align(f->hours, LV_ALIGN_CENTER, 0, -66);
        f->minutes = ui_label(f->root, "", &font_num_hero_light, f->ambient ? C_DIM : C_TEXT);
        lv_obj_align(f->minutes, LV_ALIGN_CENTER, 0, 58);
        if (cf->show_comps && !f->ambient) {
            comp_build_color(f, 0, CFG.comp_left, LV_ALIGN_CENTER, -172, 0, acc);
            comp_build_color(f, 1, CFG.comp_right, LV_ALIGN_CENTER, 172, 0, C_TEXT);
        }
    }

    if (f->ambient || previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -34);
    if (CUSTOM_FACE_COUNT > 1) {
        lv_obj_t *tap = ui_box(f->root, 200, 120);
        lv_obj_center(tap);
        lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tap, custom_cycle, LV_EVENT_SHORT_CLICKED, NULL);
    }
}

static void face_build_opts(face_t *f, lv_obj_t *root, uint8_t face, uint8_t style, bool ambient, bool edit_comps)
{
    int photo = f->photo_index;
    memset(f, 0, sizeof(*f));
    f->root = root, f->face = face, f->style = style, f->ambient = ambient, f->edit_comps = edit_comps, f->photo_index = photo;
    lv_obj_clean(root);
    if (face == FACE_ANALOG) build_analog(f);
    else if (face == FACE_MODULAR) build_modular(f);
    else if (face == FACE_PHOTO) build_photo(f);
    else if (face == FACE_MINIMAL) build_minimal(f);
    else if (face == FACE_CUSTOM) build_custom(f);
    else build_digital(f);

    if (ambient && S.batt.plugged) {
        f->charge_arc = ui_arc(root, 446, 6, C_SURF, C_GOOD, 270, 360);
        lv_obj_center(f->charge_arc);
        f->charge_text = ui_label(root, "", F20, C_GOOD);
        lv_obj_align(f->charge_text, LV_ALIGN_BOTTOM_MID, 0, -22);
    }
    face_refresh(f);
}

static void face_build(face_t *f, lv_obj_t *root, uint8_t face, bool ambient)
{
    uint8_t style = face < FACE_COUNT ? CFG.face_style[face] : 0;
    face_build_opts(f, root, face, style, ambient, false);
}

/* ------------------------------------------------------------- live face */

static void live_build(void)
{
    built_accent = CFG.accent, built_24h = CFG.clock_24h, built_custom = CFG.custom_face;
    built_custom_gen = CUSTOM_FACES_GEN;
    built_comp[0] = CFG.comp_left, built_comp[1] = CFG.comp_right;
    built_style = CFG.face < FACE_COUNT ? CFG.face_style[CFG.face] : 0;
    face_build(&live, live.root, CFG.face, false);
}

static void pick_face(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(&app_faces);
}

static void on_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    switch (e) {
    case EVT_SETTINGS: {
        uint8_t cur_style = CFG.face < FACE_COUNT ? CFG.face_style[CFG.face] : 0;
        if (CFG.face != live.face || CFG.accent != built_accent || CFG.clock_24h != built_24h ||
            CFG.comp_left != built_comp[0] || CFG.comp_right != built_comp[1] || cur_style != built_style ||
            CFG.custom_face != built_custom || (CFG.face == FACE_CUSTOM && built_custom_gen != CUSTOM_FACES_GEN)) {
            live_build();
        } else {
            status_refresh(&live);
        }
        break;
    }
    case EVT_PHOTOS:
        if (live.face == FACE_PHOTO || live.face == FACE_CUSTOM) live_build();
        break;
    case EVT_SECOND: {
        if (live.analog) lv_obj_invalidate(live.analog);
        ongoing_t o;
        bool has = shell_ongoing(&o);
        if (has || live.has_ongoing) status_refresh(&live);
        break;
    }
    case EVT_MINUTE:
        if (live.face == FACE_PHOTO && svc_photo_count() > 1) {
            live.photo_index++;
            live_build();
        } else {
            face_refresh(&live);
        }
        break;
    case EVT_DISPLAY:
        if (S.display == DISPLAY_ACTIVE) face_refresh(&live);
        break;
    case EVT_BATTERY:
    case EVT_WEATHER:
    case EVT_AGENDA:
        face_refresh(&live);
        break;
    case EVT_NOTIF:
    case EVT_LINK:
    case EVT_MEDIA:
    case EVT_CAMERA:
        status_refresh(&live);
        break;
    default: break;
    }
}

void watchface_create(lv_obj_t *tile)
{
    lv_obj_t *root = ui_box(tile, SCR, SCR);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, pick_face, LV_EVENT_LONG_PRESSED, NULL);
    bus_watch(root, on_event, NULL);
    memset(&live, 0, sizeof(live));
    live.root = root;
    live_build();
}

/* -------------------------------------------------------------- always-on */

typedef struct {
    face_t face;
    int minutes;
} aod_t;

static void aod_freed(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void aod_shift(aod_t *a)
{
    static const int8_t dx[] = {0, 8, -5, 10, -10, 4, -8, 6};
    static const int8_t dy[] = {0, -6, 8, 5, -8, 10, 4, -10};
    lv_obj_set_style_translate_x(a->face.root, dx[a->minutes % 8], 0);
    lv_obj_set_style_translate_y(a->face.root, dy[a->minutes % 8], 0);
}

lv_obj_t *aod_create(void)
{
    lv_obj_t *scr = ui_screen();
    aod_t *a = lv_malloc_zeroed(sizeof(aod_t));
    lv_obj_set_user_data(scr, a);
    lv_obj_add_event_cb(scr, aod_freed, LV_EVENT_DELETE, a);
    lv_obj_t *root = ui_box(scr, SCR, SCR);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);

    bool photos = CFG.aod_mode == AOD_PHOTOS && svc_photo_count() > 0;
    a->face.photo_index = live.photo_index;
    face_build(&a->face, root, photos ? FACE_PHOTO : CFG.face, true);
    return scr;
}

void aod_tick(lv_obj_t *aod)
{
    aod_t *a = lv_obj_get_user_data(aod);
    if (!a) return;
    a->minutes++;
    int every = CFG.photo_minutes ? CFG.photo_minutes : 1;
    if (a->face.face == FACE_PHOTO && a->minutes % every == 0 && svc_photo_count() > 1) {
        a->face.photo_index++;
        face_build(&a->face, a->face.root, FACE_PHOTO, true);
    } else {
        face_time(&a->face);
    }
    aod_shift(a);
}

/* ----------------------------------------------------- face picker & edit */

static lv_obj_t *pk_scr;
static bool pk_editing;
static uint8_t pk_edit_face;

static void picker_rebuild(void);

static void picker_choose(lv_event_t *e)
{
    CFG.face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_changed();
    shell_home();
}

static void picker_hide(lv_event_t *e)
{
    uint8_t face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    int n = visible_face_count();
    if (n <= 1) return;
    int at = -1;
    for (int i = 0; i < n; i++) {
        if (CFG.face_order[i] == face) at = i;
    }
    if (at < 0) return;
    memmove(&CFG.face_order[at], &CFG.face_order[at + 1], (size_t)(FACE_COUNT - at - 1));
    CFG.face_order[FACE_COUNT - 1] = 0xFF;
    if (CFG.face == face) CFG.face = CFG.face_order[0];
    settings_changed();
    picker_rebuild();
}

static void picker_move(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int n = visible_face_count();
    if (n < 2) return;
    int next = (idx + 1) % n;
    uint8_t tmp = CFG.face_order[idx];
    CFG.face_order[idx] = CFG.face_order[next];
    CFG.face_order[next] = tmp;
    settings_changed();
    picker_rebuild();
}

static void gallery_add_face(lv_event_t *e)
{
    uint8_t face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    int n = visible_face_count();
    if (n < FACE_COUNT) {
        CFG.face_order[n] = face;
        CFG.face = face;
        settings_changed();
    }
    shell_home();
}

static void gallery_open(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pk_scr) return;
    lv_obj_clean(pk_scr);
    lv_obj_t *list = ui_list(pk_scr);
    lv_obj_t *h = ui_label(list, "Add Watch Face", F24, C_TEXT);
    lv_obj_set_style_margin_bottom(h, 8, 0);

    bool shown[FACE_COUNT] = {false};
    int n = visible_face_count();
    for (int i = 0; i < n; i++) shown[CFG.face_order[i]] = true;

    for (int f = 0; f < FACE_COUNT; f++) {
        if (shown[f]) continue;
        lv_obj_t *b = ui_pill_btn(list, FACE_NAMES[f], C_SURF, C_TEXT, gallery_add_face, (void *)(uintptr_t)f);
        lv_obj_set_width(b, 280);
    }
    ui_list_refresh(list);
}

/* ---- Apple Watch-style Edit Mode ---- */

static face_t ed_faces[3];
static lv_obj_t *ed_style_lbl, *ed_comp_lbl[2];

static void edit_refresh_previews(void)
{
    previewing = true;
    for (int p = 0; p < 3; p++) {
        if (ed_faces[p].root) {
            face_build_opts(&ed_faces[p], ed_faces[p].root, pk_edit_face, CFG.face_style[pk_edit_face], false, p == 2);
        }
    }
    previewing = false;
    if (ed_style_lbl) lv_label_set_text(ed_style_lbl, STYLE_NAMES[pk_edit_face][CFG.face_style[pk_edit_face] % 3]);
    if (ed_comp_lbl[0]) lv_label_set_text(ed_comp_lbl[0], COMP_NAMES[CFG.comp_left % COMP_COUNT]);
    if (ed_comp_lbl[1]) lv_label_set_text(ed_comp_lbl[1], COMP_NAMES[CFG.comp_right % COMP_COUNT]);
}

static void edit_accent(lv_event_t *e)
{
    CFG.accent = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_changed();
    edit_refresh_previews();
}

static void edit_style_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    CFG.face_style[pk_edit_face] = (uint8_t)((CFG.face_style[pk_edit_face] + d + 3) % 3);
    settings_changed();
    edit_refresh_previews();
}

static void edit_comp_step(lv_event_t *e)
{
    int side = (int)(intptr_t)lv_event_get_user_data(e);
    if (side == 0) CFG.comp_left = (uint8_t)((CFG.comp_left + 1) % COMP_COUNT);
    else CFG.comp_right = (uint8_t)((CFG.comp_right + 1) % COMP_COUNT);
    settings_changed();
    edit_refresh_previews();
}

static void edit_done(lv_event_t *e)
{
    LV_UNUSED(e);
    pk_editing = false;
    CFG.face = pk_edit_face;
    settings_changed();
    shell_home();
}

static void edit_build(void)
{
    static const char *const titles[3] = {"COLOUR", "STYLE", "COMPLICATIONS"};
    pk_editing = true;
    lv_obj_clean(pk_scr);
    memset(ed_faces, 0, sizeof(ed_faces));
    ed_style_lbl = NULL;
    ed_comp_lbl[0] = ed_comp_lbl[1] = NULL;

    lv_obj_t *pages = ui_box(pk_scr, SCR, SCR);
    lv_obj_add_flag(pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);

    /* Minimal has only a colour; Custom faces are set up on the setup page. */
    int page_count = 2;
    if (pk_edit_face == FACE_DIGITAL || pk_edit_face == FACE_ANALOG || pk_edit_face == FACE_MODULAR) page_count = 3;
    else if (pk_edit_face == FACE_MINIMAL) page_count = 1;
    for (int p = 0; p < page_count; p++) {
        lv_obj_t *page = ui_box(pages, SCR, SCR);
        lv_obj_t *root = ui_box(page, SCR, SCR);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        ed_faces[p].root = root;
        ed_faces[p].photo_index = live.photo_index;

        lv_obj_t *hdr = ui_label(page, titles[p], F14, ui_accent());
        lv_obj_set_style_bg_color(hdr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_80, 0);
        lv_obj_set_style_pad_hor(hdr, 12, 0);
        lv_obj_set_style_pad_ver(hdr, 3, 0);
        lv_obj_set_style_radius(hdr, 12, 0);
        lv_obj_set_style_text_letter_space(hdr, 2, 0);
        lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 12);

        if (p == 0) {
            lv_obj_t *bar = ui_box(page, LV_SIZE_CONTENT, 48);
            lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_column(bar, 8, 0);
            lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -68);
            for (int c = 0; c < ACCENT_COUNT; c++) {
                lv_obj_t *dot = ui_round_btn(bar, 40, ui_accent_at(c), NULL, C_TEXT, edit_accent, (void *)(uintptr_t)c);
                if (c == CFG.accent) {
                    lv_obj_set_style_border_width(dot, 3, 0);
                    lv_obj_set_style_border_color(dot, C_TEXT, 0);
                }
            }
        } else if (p == 1) {
            lv_obj_t *prev = ui_round_btn(page, 48, C_SURF2, &ic_prev_28, C_TEXT, edit_style_step, (void *)(intptr_t)-1);
            lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, -94, -66);
            ed_style_lbl = ui_label(page, "", F20, C_TEXT);
            lv_obj_set_style_bg_color(ed_style_lbl, C_SURF, 0);
            lv_obj_set_style_bg_opa(ed_style_lbl, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(ed_style_lbl, 16, 0);
            lv_obj_set_style_pad_ver(ed_style_lbl, 6, 0);
            lv_obj_set_style_radius(ed_style_lbl, 16, 0);
            lv_obj_align(ed_style_lbl, LV_ALIGN_BOTTOM_MID, 0, -72);
            lv_obj_t *next = ui_round_btn(page, 48, C_SURF2, &ic_next_28, C_TEXT, edit_style_step, (void *)(intptr_t)1);
            lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 94, -66);
        } else {
            lv_obj_t *l = ui_pill_btn(page, "", C_SURF2, C_TEXT, edit_comp_step, (void *)0);
            lv_obj_set_height(l, 42);
            lv_obj_align(l, LV_ALIGN_BOTTOM_MID, -76, -68);
            ed_comp_lbl[0] = lv_obj_get_child(l, 0);
            lv_obj_t *r = ui_pill_btn(page, "", C_SURF2, C_TEXT, edit_comp_step, (void *)1);
            lv_obj_set_height(r, 42);
            lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 76, -68);
            ed_comp_lbl[1] = lv_obj_get_child(r, 0);
        }
    }

    lv_obj_t *done = ui_pill_btn(pk_scr, "Done", ui_accent(), lv_color_black(), edit_done, NULL);
    lv_obj_set_height(done, 46);
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, -12);
    edit_refresh_previews();
}

static void picker_edit(lv_event_t *e)
{
    pk_edit_face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    edit_build();
}

static void picker_rebuild(void)
{
    if (!pk_scr) return;
    pk_editing = false;
    lv_obj_clean(pk_scr);

    int n = visible_face_count();
    lv_obj_t *pages = ui_box(pk_scr, SCR, SCR);
    lv_obj_add_flag(pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);

    int active_page = 0;
    for (int i = 0; i < n; i++) {
        uint8_t f = CFG.face_order[i];
        if (f == CFG.face) active_page = i;
        lv_obj_t *page = ui_box(pages, SCR, SCR);
        lv_obj_t *root = ui_box(page, SCR, SCR);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        face_t preview = {.photo_index = live.photo_index};
        previewing = true;
        face_build(&preview, root, f, false);
        previewing = false;

        lv_obj_t *sheet = ui_box(page, SCR, SCR);
        lv_obj_add_event_cb(sheet, picker_choose, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)f);

        lv_obj_t *name = ui_label(page, FACE_NAMES[f], F16, lv_color_black());
        lv_obj_set_style_bg_color(name, f == CFG.face ? ui_accent() : C_TEXT, 0);
        lv_obj_set_style_bg_opa(name, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(name, 14, 0);
        lv_obj_set_style_pad_ver(name, 4, 0);
        lv_obj_set_style_radius(name, 14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 14);

        if (f != FACE_CUSTOM) {
            lv_obj_t *edit = ui_pill_btn(page, "Edit", C_SURF2, C_TEXT, picker_edit, (void *)(uintptr_t)f);
            lv_obj_set_height(edit, 46);
            lv_obj_align(edit, LV_ALIGN_BOTTOM_MID, n > 1 ? -56 : 0, -14);
        }
        if (n > 1) {
            lv_obj_t *mv = ui_round_btn(page, 46, C_SURF2, &ic_next_28, C_TEXT, picker_move, (void *)(intptr_t)i);
            lv_obj_align(mv, LV_ALIGN_BOTTOM_MID, 22, -14);
            lv_obj_t *hide = ui_round_btn(page, 46, ui_tint(C_BAD, 30), &ic_trash_28, C_BAD, picker_hide, (void *)(uintptr_t)f);
            lv_obj_align(hide, LV_ALIGN_BOTTOM_MID, 78, -14);
        }
    }

    if (n < FACE_COUNT) {
        lv_obj_t *add_page = ui_box(pages, SCR, SCR);
        lv_obj_t *plus = ui_round_btn(add_page, 120, C_SURF2, &ic_plus_48, ui_accent(), gallery_open, NULL);
        lv_obj_align(plus, LV_ALIGN_CENTER, 0, -24);
        lv_obj_align(ui_label(add_page, "Add Watch Face", F24, C_TEXT), LV_ALIGN_CENTER, 0, 68);
    }

    lv_obj_update_layout(pages);
    lv_obj_scroll_to_view(lv_obj_get_child(pages, active_page), LV_ANIM_OFF);
}

static bool picker_back(void)
{
    if (pk_editing) {
        picker_rebuild();
        return true;
    }
    return false;
}

static void picker_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    pk_scr = NULL;
    pk_editing = false;
}

static void picker_create(lv_obj_t *scr)
{
    pk_scr = scr;
    lv_obj_add_event_cb(scr, picker_closed, LV_EVENT_DELETE, NULL);
    picker_rebuild();
}

const app_t app_faces = {
    .id = "faces", .name = "Watch faces", .icon = &ic_clock_48, .color = 0xFFFFFF,
    .create = picker_create, .back = picker_back, .own_gestures = true, .no_clock = true,
};
