/* The Watch faces app: My Faces switcher, Gallery and Edit mode (Colour,
 * Style, Complications). The faces themselves are built by watchface.c.
 */
#include <string.h>

#include "watchface_priv.h"

const char *const FACE_NAMES[FACE_COUNT] = {"Digital", "Analog", "Modular", "Photo", "Minimal", "Custom"};
/* Minimal and Custom have no style variants. */
static const char *const STYLE_NAMES[FACE_COUNT][3] = {
    {"Stacked", "Compact", "Minimal"},
    {"Classic", "Minimal", "Sport"},
    {"Agenda", "Weather", "Pet"},
    {"Bottom", "Top", "Corner"},
};
static const char *const COMP_NAMES[COMP_COUNT] = {"Battery", "Steps", "Weather", "Day", "Pet", "Agenda", "None"};

/* ----------------------------------------------------- face picker & edit */

static lv_obj_t *pk_scr;
static bool pk_editing;
static uint8_t pk_edit_face;

static void picker_rebuild(void);
static void (*pk_pending)(void);

static void picker_deferred(void *arg)
{
    LV_UNUSED(arg);
    void (*fn)(void) = pk_pending;
    pk_pending = NULL;
    if (fn && pk_scr) fn();
}

/* Rebuilding the picker deletes the button being clicked, so it waits until
 * that button's event has finished. */
static void picker_later(void (*fn)(void))
{
    if (!pk_pending) lv_async_call(picker_deferred, NULL);
    pk_pending = fn;
}

static void picker_choose(lv_event_t *e)
{
    CFG.face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_changed();
    shell_home();
}

static void picker_hide(lv_event_t *e)
{
    uint8_t face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    int n = wf_visible_face_count();
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
    picker_later(picker_rebuild);
}

static void picker_move(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int n = wf_visible_face_count();
    if (n < 2) return;
    int next = (idx + 1) % n;
    uint8_t tmp = CFG.face_order[idx];
    CFG.face_order[idx] = CFG.face_order[next];
    CFG.face_order[next] = tmp;
    settings_changed();
    picker_later(picker_rebuild);
}

static void gallery_add_face(lv_event_t *e)
{
    uint8_t face = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    int n = wf_visible_face_count();
    if (n < FACE_COUNT) {
        CFG.face_order[n] = face;
        CFG.face = face;
        settings_changed();
    }
    shell_home();
}

static void gallery_build(void)
{
    lv_obj_clean(pk_scr);
    lv_obj_t *list = ui_list(pk_scr);
    lv_obj_t *h = ui_label(list, "Add Watch Face", F24, C_TEXT);
    lv_obj_set_style_margin_bottom(h, 8, 0);

    bool shown[FACE_COUNT] = {false};
    int n = wf_visible_face_count();
    for (int i = 0; i < n; i++) shown[CFG.face_order[i]] = true;

    for (int f = 0; f < FACE_COUNT; f++) {
        if (shown[f]) continue;
        lv_obj_t *b = ui_pill_btn(list, FACE_NAMES[f], C_SURF, C_TEXT, gallery_add_face, (void *)(uintptr_t)f);
        lv_obj_set_width(b, 280);
    }
    ui_list_refresh(list);
}

static void gallery_open(lv_event_t *e)
{
    LV_UNUSED(e);
    picker_later(gallery_build);
}

/* ---- Apple Watch-style Edit Mode ---- */

enum { ED_COLOUR, ED_STYLE, ED_COMPS, ED_SLOT };
static face_t ed_faces[3];
static uint8_t ed_kind[3];
static lv_obj_t *ed_style_lbl, *ed_comp_lbl[2];

static void edit_refresh_previews(void)
{
    wf_previewing = true;
    for (int p = 0; p < 3; p++) {
        if (ed_faces[p].root) {
            wf_build_opts(&ed_faces[p], ed_faces[p].root, pk_edit_face, CFG.face_style[pk_edit_face], false,
                            ed_kind[p] == ED_COMPS);
        }
    }
    wf_previewing = false;
    if (ed_style_lbl) {
        if (pk_edit_face == FACE_CUSTOM) {
            lv_label_set_text(ed_style_lbl, CUSTOM_FACES[CFG.custom_face < CUSTOM_FACE_COUNT ? CFG.custom_face : 0].name);
        } else {
            lv_label_set_text(ed_style_lbl, STYLE_NAMES[pk_edit_face][CFG.face_style[pk_edit_face] % 3]);
        }
    }
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

/* Custom: step through the faces saved on the setup page. */
static void edit_slot_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (CUSTOM_FACE_COUNT <= 1) return;
    CFG.custom_face = (uint8_t)((CFG.custom_face + d + CUSTOM_FACE_COUNT) % CUSTOM_FACE_COUNT);
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
    static const char *const titles[] = {"COLOUR", "STYLE", "COMPLICATIONS", "FACE"};
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

    /* Custom faces carry their own colour and layout; Minimal has no style or
     * complications. */
    int page_count = 0;
    if (pk_edit_face == FACE_CUSTOM) {
        ed_kind[page_count++] = ED_SLOT;
    } else {
        ed_kind[page_count++] = ED_COLOUR;
        if (pk_edit_face != FACE_MINIMAL) ed_kind[page_count++] = ED_STYLE;
        if (pk_edit_face == FACE_DIGITAL || pk_edit_face == FACE_ANALOG || pk_edit_face == FACE_MODULAR) {
            ed_kind[page_count++] = ED_COMPS;
        }
    }
    for (int p = 0; p < page_count; p++) {
        lv_obj_t *page = ui_box(pages, SCR, SCR);
        lv_obj_t *root = ui_box(page, SCR, SCR);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        ed_faces[p].root = root;
        ed_faces[p].photo_index = wf_photo_index();

        lv_obj_t *hdr = ui_label(page, titles[ed_kind[p]], F14, ui_accent());
        lv_obj_set_style_bg_color(hdr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_80, 0);
        lv_obj_set_style_pad_hor(hdr, 12, 0);
        lv_obj_set_style_pad_ver(hdr, 3, 0);
        lv_obj_set_style_radius(hdr, 12, 0);
        lv_obj_set_style_text_letter_space(hdr, 2, 0);
        lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 12);

        if (ed_kind[p] == ED_COLOUR) {
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
        } else if (ed_kind[p] == ED_STYLE || ed_kind[p] == ED_SLOT) {
            lv_event_cb_t step = ed_kind[p] == ED_SLOT ? edit_slot_step : edit_style_step;
            lv_obj_t *prev = ui_round_btn(page, 48, C_SURF2, &ic_prev_28, C_TEXT, step, (void *)(intptr_t)-1);
            lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, -94, -66);
            ed_style_lbl = ui_label(page, "", F20, C_TEXT);
            lv_obj_set_style_bg_color(ed_style_lbl, C_SURF, 0);
            lv_obj_set_style_bg_opa(ed_style_lbl, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(ed_style_lbl, 16, 0);
            lv_obj_set_style_pad_ver(ed_style_lbl, 6, 0);
            lv_obj_set_style_radius(ed_style_lbl, 16, 0);
            lv_obj_align(ed_style_lbl, LV_ALIGN_BOTTOM_MID, 0, -72);
            lv_obj_t *next = ui_round_btn(page, 48, C_SURF2, &ic_next_28, C_TEXT, step, (void *)(intptr_t)1);
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
    picker_later(edit_build);
}

static void picker_rebuild(void)
{
    if (!pk_scr) return;
    pk_editing = false;
    lv_obj_clean(pk_scr);

    int n = wf_visible_face_count();
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
        face_t preview = {.photo_index = wf_photo_index()};
        wf_previewing = true;
        wf_build(&preview, root, f, false);
        wf_previewing = false;

        lv_obj_t *sheet = ui_box(page, SCR, SCR);
        lv_obj_add_event_cb(sheet, picker_choose, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)f);

        lv_obj_t *name = ui_label(page, FACE_NAMES[f], F16, lv_color_black());
        lv_obj_set_style_bg_color(name, f == CFG.face ? ui_accent() : C_TEXT, 0);
        lv_obj_set_style_bg_opa(name, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(name, 14, 0);
        lv_obj_set_style_pad_ver(name, 4, 0);
        lv_obj_set_style_radius(name, 14, 0);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 14);

        lv_obj_t *edit = ui_pill_btn(page, "Edit", C_SURF2, C_TEXT, picker_edit, (void *)(uintptr_t)f);
        lv_obj_set_height(edit, 46);
        lv_obj_align(edit, LV_ALIGN_BOTTOM_MID, n > 1 ? -56 : 0, -14);
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
