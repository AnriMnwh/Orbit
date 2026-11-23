/* Watch faces (digital, analog, photo, and the Vision and TV wall that live
 * with their apps), their dimmed always-on versions, and the face picker.
 *
 * A face is built into a face_t. There is one live face on the home screen;
 * the always-on screen and the picker build their own copies with the same
 * code, so what you pick is what you get and what rests is what was showing.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"


static const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *hours, *minutes, *time, *date;
    lv_obj_t *comp_arc[3], *comp_text[3];
    uint8_t comp_kind[3];
    lv_obj_t *status, *analog, *charge_arc, *charge_text;
    uint8_t face;
    bool ambient; /* the dim always-on version: fewer lit pixels, no seconds */
    int photo_index;
} face_t;

static face_t live;
static uint8_t built_accent, built_24h, built_comp[2], built_custom;
static uint32_t built_custom_gen;
static bool previewing; /* set while the picker builds its copies */
static char last_ongoing[32];

static void fmt_date(char *buf, size_t len, const struct tm *t)
{
    snprintf(buf, len, "%s %d %s", DAYS[t->tm_wday % 7], t->tm_mday, MONTHS[t->tm_mon % 12]);
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
 * camera), otherwise small status glyphs. */
static void status_refresh(face_t *f)
{
    if (!f->status) return;
    lv_obj_clean(f->status);
    lv_obj_set_style_bg_opa(f->status, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(f->status, LV_OBJ_FLAG_CLICKABLE);

    ongoing_t o;
    if (shell_ongoing(&o)) {
        lv_obj_set_style_bg_color(f->status, C_SURF, 0);
        lv_obj_set_style_bg_opa(f->status, LV_OPA_COVER, 0);
        lv_obj_add_flag(f->status, LV_OBJ_FLAG_CLICKABLE);
        ui_icon(f->status, o.icon, ui_accent());
        lv_obj_t *l = ui_label(f->status, o.text, F16, C_TEXT);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(l, 150, 0);
        return;
    }
    char buf[16];
    if (S.notif_count > 0) {
        snprintf(buf, sizeof(buf), "%d", S.notif_count);
        status_item(f->status, &ic_bell_28, ui_accent(), buf);
    }
    if (S.link.keys_mode) status_item(f->status, &ic_keys_28, C_DIM, NULL);
    else if (S.link.midi_mode) status_item(f->status, &ic_music_28, C_DIM, NULL);
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

static const lv_image_dsc_t *comp_icon(int kind)
{
    switch (kind) {
    case COMP_BATTERY: return &ic_bolt_28;
    case COMP_STEPS: return &ic_steps_28;
    case COMP_WEATHER: return &ic_cloud_28;
    default: return &ic_clock_28;
    }
}

static void comp_build_at(face_t *f, int slot, int kind, int x, int y, lv_color_t color)
{
    if (kind == COMP_NONE || slot < 0 || slot >= 3) return;
    f->comp_kind[slot] = (uint8_t)kind;
    lv_obj_t *box = ui_box(f->root, 84, 84);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(box, LV_ALIGN_CENTER, x, y);
    f->comp_arc[slot] = ui_arc(box, 84, 7, C_SURF2, color, 135, 270);
    lv_obj_center(f->comp_arc[slot]);
    f->comp_text[slot] = ui_label(box, "", F20, C_TEXT);
    lv_obj_align(f->comp_text[slot], LV_ALIGN_CENTER, 0, -3);
    lv_obj_t *i = ui_icon(box, comp_icon(kind), color);
    lv_image_set_scale(i, 150);
    lv_obj_align(i, LV_ALIGN_BOTTOM_MID, 0, 6);
}

static void comp_build(face_t *f, int side, int kind)
{
    comp_build_at(f, side, kind, side == 0 ? -172 : 172, 0, side == 0 ? ui_accent() : C_TEXT);
}

static void comp_refresh(face_t *f, int side, int kind)
{
    if (!f->comp_arc[side]) return;
    char buf[12] = "";
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
        percent = (int)(S.steps * 100 / CFG.step_goal);
        if (S.steps >= 10000) snprintf(buf, sizeof(buf), "%uk", (unsigned)(S.steps / 1000));
        else if (S.steps >= 1000) snprintf(buf, sizeof(buf), "%u.%uk", (unsigned)(S.steps / 1000), (unsigned)(S.steps % 1000 / 100));
        else snprintf(buf, sizeof(buf), "%u", (unsigned)S.steps);
        break;
    case COMP_WEATHER:
        percent = 100;
        if (S.weather.valid) snprintf(buf, sizeof(buf), "%d\xC2\xB0", S.weather.temp_c);
        else snprintf(buf, sizeof(buf), "--");
        break;
    default: /* how much of the day has gone */
        svc_localtime(&t);
        percent = (t.tm_hour * 60 + t.tm_min) * 100 / 1440;
        snprintf(buf, sizeof(buf), "%d", t.tm_mday);
        break;
    }
    ui_arc_set(f->comp_arc[side], percent);
    lv_label_set_text(f->comp_text[side], buf);
}

/* ------------------------------------------------------------ analog dial */

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
    lv_obj_t *obj = lv_event_get_target_obj(e);
    bool ambient = lv_event_get_user_data(e) != NULL;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    struct tm t;
    svc_localtime(&t);
    lv_color_t acc = ui_accent();

    for (int i = 0; i < 60; i += ambient ? 5 : 1) {
        bool major = i % 5 == 0;
        stroke(layer, &a, i * 6.0f, major ? 196 : 208, 218, major ? (ambient ? 3 : 5) : 2,
               major ? (ambient ? C_FAINT : C_TEXT) : C_FAINT);
    }
    float m = t.tm_min + (ambient ? 0 : t.tm_sec / 60.0f);
    float h = (t.tm_hour % 12) + m / 60.0f;
    stroke(layer, &a, h * 30.0f, 0, 104, ambient ? 6 : 13, ambient ? C_DIM : C_TEXT);
    stroke(layer, &a, m * 6.0f, 0, 164, ambient ? 4 : 9, ambient ? C_DIM : C_TEXT);
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
    for (int i = 0; i < 3; i++) {
        if (f->comp_arc[i]) comp_refresh(f, i, f->comp_kind[i]);
    }
    status_refresh(f);
}

static void build_digital(face_t *f)
{
    lv_color_t acc = ui_accent();
    f->date = ui_label(f->root, "", F20, C_DIM);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 50);

    /* Hours over minutes. Awake: heavy accent over light white. Resting: both
     * thin and grey, so far fewer pixels are lit. */
    f->hours = ui_label(f->root, "", f->ambient ? &font_num_hero_light : &font_num_hero_bold,
                        f->ambient ? ui_tint(acc, 70) : acc);
    lv_obj_align(f->hours, LV_ALIGN_CENTER, 0, -66);
    f->minutes = ui_label(f->root, "", &font_num_hero_light, f->ambient ? C_DIM : C_TEXT);
    lv_obj_align(f->minutes, LV_ALIGN_CENTER, 0, 58);
    if (f->ambient) return;

    comp_build(f, 0, CFG.comp_left);
    comp_build(f, 1, CFG.comp_right);
    if (previewing) return; /* the picker shows the face itself, not what is running */
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -38);
}

static void build_analog(face_t *f)
{
    f->analog = ui_box(f->root, SCR, SCR);
    lv_obj_remove_flag(f->analog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(f->analog, analog_draw, LV_EVENT_DRAW_MAIN, f->ambient ? (void *)1 : NULL);

    f->date = ui_label(f->root, "", F20, f->ambient ? C_FAINT : C_DIM);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 96);
    if (f->ambient || previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_CENTER, 0, -96);
}

static void build_photo(face_t *f)
{
    int count = svc_photo_count();
    const lv_image_dsc_t *img = count > 0 ? svc_photo(f->photo_index % count) : NULL;
    if (img) {
        lv_obj_t *photo = lv_image_create(f->root);
        lv_image_set_src(photo, img);
        lv_obj_center(photo);
        /* Darken the lower half so the time stays readable on any picture. */
        lv_obj_t *shade = ui_box(f->root, SCR, 250);
        lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(shade, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_color(shade, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_dir(shade, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(shade, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_opa(shade, 220, 0);
        lv_obj_set_style_bg_main_opa(shade, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_t *hint = ui_label(f->root, "Add photos from your phone\nSettings  >  Wi-Fi shows how", F20, C_DIM);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, -70);
    }
    f->time = ui_label(f->root, "", f->ambient ? &font_num_mid : &font_num_big, C_TEXT);
    lv_obj_align(f->time, LV_ALIGN_BOTTOM_MID, 0, f->ambient ? -92 : -84);
    f->date = ui_label(f->root, "", F20, C_TEXT);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_BOTTOM_MID, 0, -52);
}

/* The gem fills the screen; the time sits in its darker lower half. */
static void build_vision(face_t *f)
{
    vision_face(f->root, f->ambient, !f->ambient && !previewing);
    f->time = ui_label(f->root, "", &font_num_mid, f->ambient ? C_DIM : C_TEXT);
    lv_obj_align(f->time, LV_ALIGN_CENTER, 0, 84);
    f->date = ui_label(f->root, "", F16, f->ambient ? C_DIM : C_TEXT);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_CENTER, 0, 130);
    if (f->ambient || previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_CENTER, 0, -150);
}

/* The TV wall shows the time and date itself. */
static void build_zzz(face_t *f)
{
    zzz_face(f->root, f->ambient, !f->ambient && !previewing);
    if (f->ambient || previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -26);
}

/* Wear OS Modular dashboard: date at top, crisp horizontal clock, three live
 * complication pods across the lower half. */
static void build_modular(face_t *f)
{
    lv_color_t acc = ui_accent();
    f->date = ui_label(f->root, "", F20, f->ambient ? C_FAINT : acc);
    lv_obj_set_style_text_letter_space(f->date, 3, 0);
    lv_obj_align(f->date, LV_ALIGN_TOP_MID, 0, 56);

    f->time = ui_label(f->root, "", f->ambient ? &font_num_mid : &font_num_big, f->ambient ? C_DIM : C_TEXT);
    lv_obj_align(f->time, LV_ALIGN_CENTER, 0, f->ambient ? 0 : -44);
    if (f->ambient) return;

    comp_build_at(f, 0, COMP_BATTERY, -106, 76, acc);
    comp_build_at(f, 1, COMP_STEPS, 0, 102, C_TEXT);
    comp_build_at(f, 2, COMP_WEATHER, 106, 76, lv_color_hex(0x58B6FF));
    if (previewing) return;
    f->status = status_row(f->root);
    lv_obj_align(f->status, LV_ALIGN_BOTTOM_MID, 0, -26);
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
            comp_build_at(f, 0, CFG.comp_left, -74, 82, acc);
            comp_build_at(f, 1, CFG.comp_right, 74, 82, C_TEXT);
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
            comp_build_at(f, 0, CFG.comp_left, -172, 0, acc);
            comp_build_at(f, 1, CFG.comp_right, 172, 0, C_TEXT);
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

static void face_build(face_t *f, lv_obj_t *root, uint8_t face, bool ambient)
{
    int photo = f->photo_index;
    memset(f, 0, sizeof(*f));
    f->root = root, f->face = face, f->ambient = ambient, f->photo_index = photo;
    lv_obj_clean(root);
    if (face == FACE_ANALOG) build_analog(f);
    else if (face == FACE_PHOTO) build_photo(f);
    else if (face == FACE_VISION) build_vision(f);
    else if (face == FACE_ZZZ) build_zzz(f);
    else if (face == FACE_MODULAR) build_modular(f);
    else if (face == FACE_MINIMAL) build_minimal(f);
    else if (face == FACE_CUSTOM) build_custom(f);
    else build_digital(f);

    /* On the charger, the resting face also shows how full the battery is. */
    if (ambient && S.batt.plugged) {
        f->charge_arc = ui_arc(root, 446, 6, C_SURF, C_GOOD, 270, 360);
        lv_obj_center(f->charge_arc);
        f->charge_text = ui_label(root, "", F20, C_GOOD);
        lv_obj_align(f->charge_text, LV_ALIGN_BOTTOM_MID, 0, -22);
    }
    face_refresh(f);
}

/* ------------------------------------------------------------- live face */

static void live_build(void)
{
    built_accent = CFG.accent, built_24h = CFG.clock_24h, built_custom = CFG.custom_face;
    built_custom_gen = CUSTOM_FACES_GEN;
    built_comp[0] = CFG.comp_left, built_comp[1] = CFG.comp_right;
    last_ongoing[0] = 0;
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
    case EVT_SETTINGS:
        if (CFG.face != live.face || CFG.accent != built_accent || CFG.clock_24h != built_24h ||
            CFG.comp_left != built_comp[0] || CFG.comp_right != built_comp[1] ||
            CFG.custom_face != built_custom ||
            (CFG.face == FACE_CUSTOM && built_custom_gen != CUSTOM_FACES_GEN)) live_build();
        else status_refresh(&live);
        break;
    case EVT_PHOTOS:
        if (live.face == FACE_PHOTO || live.face == FACE_CUSTOM) live_build();
        break;
    case EVT_SECOND: {
        if (live.analog) lv_obj_invalidate(live.analog);
        /* Only rebuild the ongoing chip when its text actually changes. */
        ongoing_t o;
        const char *now_txt = shell_ongoing(&o) ? o.text : "";
        if (strcmp(now_txt, last_ongoing) != 0) {
            strlcpy(last_ongoing, now_txt, sizeof(last_ongoing));
            status_refresh(&live);
        }
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
        for (int i = 0; i < 3; i++) {
            if (live.comp_arc[i]) comp_refresh(&live, i, live.comp_kind[i]);
        }
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

/* The resting screen redraws once a minute and nudges its contents each
 * time, so no pixel is lit in the same place for long (OLED burn-in). */
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

    /* "Always-on photos" shows your pictures whatever the face; otherwise the
     * face you were looking at simply dims. */
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

/* ------------------------------------------------------------ face picker */

static void picker_choose(lv_event_t *e)
{
    CFG.face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_changed();
    shell_home();
}

static void picker_customise(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(app_find("settings"));
}

/* Swipe through the faces at full size and tap the one you want. */
static void picker_create(lv_obj_t *scr)
{
    static const char *const names[FACE_COUNT] = {
        "Digital", "Analog", "Photo", "Vision", "TV wall", "Modular", "Minimal", "Custom",
    };
    static face_t previews[FACE_COUNT];

    lv_obj_t *pages = ui_box(scr, SCR, SCR);
    lv_obj_add_flag(pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);

    for (int i = 0; i < FACE_COUNT; i++) {
        lv_obj_t *page = ui_box(pages, SCR, SCR);
        lv_obj_t *root = ui_box(page, SCR, SCR);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        previews[i].photo_index = live.photo_index;
        previewing = true;
        face_build(&previews[i], root, (uint8_t)i, false);
        previewing = false;
        /* A sheet over the preview takes the tap, so nothing inside it reacts. */
        lv_obj_t *sheet = ui_box(page, SCR, SCR);
        lv_obj_add_event_cb(sheet, picker_choose, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = ui_label(page, names[i], F16, lv_color_black());
        lv_obj_set_style_bg_color(name, i == CFG.face ? ui_accent() : C_TEXT, 0);
        lv_obj_set_style_bg_opa(name, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(name, 14, 0);
        lv_obj_set_style_pad_ver(name, 4, 0);
        lv_obj_set_style_radius(name, 14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 14);
    }
    lv_obj_update_layout(pages);
    lv_obj_scroll_to_view(lv_obj_get_child(pages, CFG.face), LV_ANIM_OFF);

    lv_obj_t *custom = ui_round_btn(scr, 56, C_SURF2, &ic_gear_28, C_TEXT, picker_customise, NULL);
    lv_obj_align(custom, LV_ALIGN_BOTTOM_MID, 0, -6);
}

const app_t app_faces = {
    .id = "faces", .name = "Watch faces", .icon = &ic_clock_48, .color = 0xFFFFFF,
    .create = picker_create, .own_gestures = true, .no_clock = true,
};
