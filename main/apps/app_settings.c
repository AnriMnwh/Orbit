/* Settings, and the first-boot setup that walks through the important ones.
 *
 * Both are a stack of pages inside one app screen. A page is just a function
 * that fills `root`; most build a scrolling list of rows, a few (keyboard,
 * QR code, setup steps) lay themselves out freely. Whenever something a page
 * shows might have changed, the top page is simply rebuilt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_priv.h"

#define DEPTH 6

static lv_obj_t *root, *list;
static page_fn stack[DEPTH];
static int scroll_y[DEPTH], depth;
static bool keep;      /* the top page holds typed text: do not rebuild it under the user */
static bool show_pending;

static void show_now(void)
{
    show_pending = false;
    if (!root || depth == 0) return;
    lv_obj_clean(root);
    list = NULL, keep = false;
    stack[depth - 1]();
    if (!list) return;
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll_y[depth - 1], LV_ANIM_OFF);
    ui_list_refresh(list);
}

static void show_async(void *arg)
{
    LV_UNUSED(arg);
    if (show_pending) show_now();
}

/* Defer rebuilding when `root` already has children so we never free a button
 * synchronously inside its own LV_EVENT_CLICKED / SHORT_CLICKED handler. */
static void show(void)
{
    if (!root || depth == 0) return;
    if (lv_obj_get_child_count(root) == 0) {
        show_now();
        return;
    }
    if (!show_pending) {
        show_pending = true;
        lv_async_call(show_async, NULL);
    }
}

void st_refresh(void)
{
    if (keep) return;
    if (list) scroll_y[depth - 1] = lv_obj_get_scroll_y(list);
    show();
}

static void push(page_fn page)
{
    if (depth >= DEPTH) return;
    if (list) scroll_y[depth - 1] = lv_obj_get_scroll_y(list);
    stack[depth++] = page;
    scroll_y[depth - 1] = 0;
    show();
}

/* Makes `page` the only page, for the setup steps. */
void st_run_page(page_fn page)
{
    depth = 1;
    stack[0] = page;
    scroll_y[0] = 0;
    show();
}

lv_obj_t *st_root(void)
{
    return root;
}

static void pop_to(int level)
{
    if (level < 1 || level >= depth) return;
    depth = level;
    show();
}

static void changed(void)
{
    settings_changed(); /* comes back round as EVT_SETTINGS and rebuilds the page */
}

/* -------------------------------------------------------------------- rows */

static lv_obj_t *chip(int height)
{
    lv_obj_t *r = ui_box(list, 344, height);
    lv_obj_set_style_radius(r, height / 2, 0);
    lv_obj_set_style_bg_color(r, C_SURF, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

static void page_title(const char *title)
{
    list = ui_list(root);
    lv_obj_t *h = ui_label(list, title, F24, C_TEXT);
    lv_obj_set_style_margin_bottom(h, 6, 0);
}

/* One tappable row: icon, label, and a value on the right (accent when `on`). */
static lv_obj_t *row(const lv_image_dsc_t *icon, const char *label, const char *value, bool on, lv_event_cb_t cb, void *arg)
{
    lv_obj_t *r = chip(72);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(r, 22, 0);
    lv_obj_set_style_pad_column(r, 12, 0);
    if (cb) {
        lv_obj_set_style_bg_color(r, C_SURF2, LV_STATE_PRESSED);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, cb, LV_EVENT_SHORT_CLICKED, arg);
    } else {
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    }
    if (icon) ui_icon(r, icon, on ? ui_accent() : C_DIM);
    lv_obj_t *l = ui_label(r, label, F20, C_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(l, 1);
    if (value && value[0]) {
        lv_obj_t *v = ui_label(r, value, F20, on ? ui_accent() : C_DIM);
        lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(v, 150, 0);
    }
    return r;
}

static void note(const char *text)
{
    lv_obj_t *l = ui_label(list, text, F16, C_DIM);
    lv_obj_set_width(l, 300);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void slider_row(const lv_image_dsc_t *icon, int value, int min, lv_event_cb_t cb)
{
    lv_obj_t *r = chip(72);
    lv_obj_align(ui_icon(r, icon, C_DIM), LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_t *s = lv_slider_create(r);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 224, 14);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 78, 0);
    lv_slider_set_range(s, min, 100);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(s, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, C_SURF2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, ui_accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 9, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 24);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);
}

static const char *on_off(bool v)
{
    return v ? "On" : "Off";
}

/* Flips the settings byte passed as the row's argument. */
void st_toggle(lv_event_t *e)
{
    uint8_t *field = lv_event_get_user_data(e);
    *field = !*field;
    changed();
}

void st_open_page(lv_event_t *e)
{
    push((page_fn)lv_event_get_user_data(e));
}

lv_obj_t *st_free_title(const char *text, int y)
{
    lv_obj_t *l = ui_label(root, text, F16, C_DIM);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

lv_obj_t *st_free_text(const char *text, const lv_font_t *font, lv_color_t color, int y)
{
    lv_obj_t *l = ui_label(root, text, font, color);
    lv_obj_set_width(l, 330);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

/* ------------------------------------------------------------------- Wi-Fi */

static char pick_ssid[33];

const char *st_pick_ssid(void)
{
    return pick_ssid;
}
static int wifi_home; /* stack level to return to once a network is chosen */

static void qr(const char *payload, int y)
{
    lv_obj_t *plate = ui_box(root, 168, 168);
    lv_obj_set_style_bg_color(plate, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(plate, 18, 0);
    lv_obj_align(plate, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *code = lv_qrcode_create(plate);
    lv_qrcode_set_size(code, 140);
    lv_qrcode_set_dark_color(code, lv_color_black());
    lv_qrcode_set_light_color(code, lv_color_white());
    lv_qrcode_update(code, payload, (uint32_t)strlen(payload));
    lv_obj_center(code);
}

static void hotspot_toggle(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.wifi.setup_mode) {
        svc_wifi_setup_mode(false);
        svc_web_enable(false);
    } else {
        svc_web_enable(true);
        svc_wifi_setup_mode(true);
    }
}

static void web_stop(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_web_enable(false);
}

/* The phone route: a QR code that opens the setup page, where photos, keys,
 * the assistant and the camera are configured. */
void st_phone_page(void)
{
    char buf[112];
    st_free_title("SET UP FROM PHONE", 34);
    if (S.wifi.setup_mode) {
        if (!S.wifi.web_active) svc_web_enable(true);
        /* Phones offer to join the network in this code; the page then opens by itself. */
        snprintf(buf, sizeof(buf), "WIFI:T:WPA;S:Orbit-Setup;P:%s;;", S.wifi.setup_pass);
        qr(buf, 64);
        st_free_text("Scan to join Orbit-Setup", F20, C_TEXT, 240);
        snprintf(buf, sizeof(buf), "Pass %s  " LV_SYMBOL_BULLET "  PIN %s", S.wifi.setup_pass, S.wifi.web_pin);
        st_free_text(buf, F16, C_DIM, 270);
        lv_obj_align(ui_pill_btn(root, "Stop", C_SURF2, C_TEXT, hotspot_toggle, NULL), LV_ALIGN_BOTTOM_MID, 0, -48);
    } else if (S.wifi.connected) {
        if (!S.wifi.web_active) svc_web_enable(true);
        snprintf(buf, sizeof(buf), "http://%s/?pin=%s", S.wifi.ip, S.wifi.web_pin);
        qr(buf, 64);
        st_free_text("Scan to open setup page", F20, C_TEXT, 240);
        snprintf(buf, sizeof(buf), "orbit.local  " LV_SYMBOL_BULLET "  PIN %s", S.wifi.web_pin);
        st_free_text(buf, F16, ui_accent(), 270);
        lv_obj_align(ui_pill_btn(root, "Stop page", C_SURF2, C_TEXT, web_stop, NULL), LV_ALIGN_BOTTOM_MID, 0, -48);
    } else {
        lv_obj_align(ui_icon(root, &ic_phone_48, C_FAINT), LV_ALIGN_TOP_MID, 0, 92);
        st_free_text("No Wi-Fi yet", F28, C_TEXT, 156);
        st_free_text("Start a temporary hotspot or turn on Wi-Fi to set up from your phone.", F20, C_DIM, 200);
        lv_obj_align(ui_pill_btn(root, "Start hotspot", ui_accent(), lv_color_black(), hotspot_toggle, NULL),
                     LV_ALIGN_BOTTOM_MID, 0, -58);
    }
}

/* ---- typing a password on a round screen ---- */

/* Rows get narrower towards the bottom, following the glass. The blank
 * entries are invisible spacers that push the keys in from the sides. */
#define PAD(w) (LV_BUTTONMATRIX_CTRL_HIDDEN | (w))
#define FN(w) (LV_KEYBOARD_CTRL_BUTTON_FLAGS | (w))

static const char *const KB_LOWER[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    " ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
    " ", "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, " ", "\n",
    " ", "1#", " ", ".", LV_SYMBOL_OK, " ", ""};
static const char *const KB_UPPER[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    " ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
    " ", "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, " ", "\n",
    " ", "1#", " ", ".", LV_SYMBOL_OK, " ", ""};
static const char *const KB_SYMBOL[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    " ", "@", "#", "$", "_", "&", "-", "+", "(", ")", " ", "\n",
    " ", "abc", "!", "\"", "'", ":", ";", "/", "?", LV_SYMBOL_BACKSPACE, " ", "\n",
    " ", ",", " ", ".", LV_SYMBOL_OK, " ", ""};
static const lv_buttonmatrix_ctrl_t KB_CTRL[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    PAD(2), 4, 4, 4, 4, 4, 4, 4, 4, 4, PAD(2),
    PAD(4), FN(5), 4, 4, 4, 4, 4, 4, 4, LV_BUTTONMATRIX_CTRL_CHECKED | 5, PAD(4),
    PAD(10), FN(5), 12, 4, FN(6), PAD(10)};

static void password_done(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_user_data(e);
    svc_wifi_join(pick_ssid, lv_textarea_get_text(ta));
    pop_to(wifi_home);
}

static void password_page(void)
{
    keep = true;
    lv_color_t acc = ui_accent();
    lv_obj_align(ui_label_fit(root, pick_ssid, F16, C_DIM, 240), LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *ta = lv_textarea_create(root);
    lv_obj_remove_style_all(ta);
    lv_obj_set_size(ta, 300, 52);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 86);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 63);
    lv_textarea_set_placeholder_text(ta, "Password");
    lv_obj_set_style_radius(ta, 26, 0);
    lv_obj_set_style_bg_color(ta, C_SURF, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(ta, 20, 0);
    lv_obj_set_style_pad_ver(ta, 13, 0);
    lv_obj_set_style_text_font(ta, F20, 0);
    lv_obj_set_style_text_color(ta, C_TEXT, 0);
    lv_obj_set_style_text_color(ta, C_FAINT, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(ta, acc, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);

    lv_obj_t *kb = lv_keyboard_create(root);
    lv_obj_remove_style_all(kb);
    lv_obj_set_size(kb, 436, 232);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 150);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SYMBOL, KB_CTRL);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_pad_row(kb, 5, 0);
    lv_obj_set_style_pad_column(kb, 4, 0);
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_SURF2, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_FAINT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, C_SURF, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, F20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, acc, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(kb, password_done, LV_EVENT_READY, ta);
}

/* ---- choosing a network ---- */

static void network_picked(lv_event_t *e)
{
    const wifi_net_t *n = &S.scan[(int)(intptr_t)lv_event_get_user_data(e)];
    snprintf(pick_ssid, sizeof(pick_ssid), "%s", n->ssid);
    if (n->secure) {
        push(password_page);
    } else {
        svc_wifi_join(pick_ssid, "");
        pop_to(wifi_home);
    }
}

static void rescan(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_wifi_scan();
}

static void networks_page(void)
{
    page_title("Networks");
    if (S.scanning) {
        note("Searching...");
        return;
    }
    for (int i = 0; i < S.scan_count; i++) {
        const wifi_net_t *n = &S.scan[i];
        const char *bars = n->rssi > -60 ? "Strong" : (n->rssi > -75 ? "Good" : "Weak");
        row(n->secure ? &ic_wifi_28 : &ic_link_28, n->ssid, bars, false, network_picked, (void *)(intptr_t)i);
    }
    if (S.scan_count == 0) note("No networks found. Only 2.4 GHz networks can be seen.");
    row(&ic_restart_28, "Search again", NULL, false, rescan, NULL);
}

void st_choose_network(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_home = depth;
    svc_wifi_scan();
    push(networks_page);
}

static void forget_yes(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_wifi_forget();
}

static void forget_network(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Forget this Wi-Fi network?", "Forget", forget_yes);
}

static const char *wifi_summary(void)
{
    if (S.wifi.joining) return "Joining";
    if (S.wifi.connected) return S.wifi.ssid;
    if (S.wifi.failed) return "Failed";
    return S.wifi.setup_mode ? "Hotspot" : "Off";
}

static void wifi_page(void)
{
    page_title("Wi-Fi");
    row(&ic_wifi_28, S.wifi.connected ? "Connected" : "Status", wifi_summary(), S.wifi.connected, NULL, NULL);
    if (S.wifi.failed) note("Could not join. Check the password and that the network is 2.4 GHz.");
    if (S.wifi.connected) row(NULL, "Address", S.wifi.ip, false, NULL, NULL);
    row(&ic_target_28, "Choose network", NULL, false, st_choose_network, NULL);
    row(&ic_phone_28, "Set up from phone", NULL, false, st_open_page, (void *)st_phone_page);
    if (svc_configured(HAS_WIFI)) row(&ic_trash_28, "Forget network", NULL, false, forget_network, NULL);
}

/* --------------------------------------------------------------- Bluetooth */

static void bt_mode(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_link_keys_mode(!S.link.keys_mode);
}

static void bt_forget_yes(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_link_forget();
    shell_toast("Pairings removed");
}

static void bt_forget(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Forget all paired devices?", "Forget", bt_forget_yes);
}

static void bt_page(void)
{
    bool up = S.link.status == LINK_CONNECTED;
    const char *mode = S.link.keys_mode ? "Keyboard" : "Phone";
    page_title("Bluetooth");
    row(&ic_bluetooth_28, up ? (S.link.name[0] ? S.link.name : "Connected") : "Not connected", up ? "On" : "Waiting", up, NULL,
        NULL);
    row(&ic_link_28, "Act as", mode, true, bt_mode, NULL);
    row(&ic_trash_28, "Forget devices", NULL, false, bt_forget, NULL);
    note(S.link.keys_mode ? "Pair \"Orbit Keys\" in your computer's Bluetooth settings."
                          : "iPhone: pair \"Orbit\" in Bluetooth settings.\nAndroid: add it in Gadgetbridge.");
}

static void connectivity_page(void)
{
    page_title("Connectivity");
    row(&ic_wifi_28, "Wi-Fi", wifi_summary(), S.wifi.connected, st_open_page, (void *)wifi_page);
    row(&ic_bluetooth_28, "Bluetooth", S.link.status == LINK_CONNECTED ? "Connected" : "Waiting",
        S.link.status == LINK_CONNECTED, st_open_page, (void *)bt_page);
    row(&ic_phone_28, "Set up from phone", NULL, false, st_open_page, (void *)st_phone_page);
}

/* ----------------------------------------------------------------- display */

static const uint8_t TIMEOUTS[] = {8, 12, 20, 30, 60};
static const char *const AOD_NAMES[AOD_COUNT] = {"Off", "Clock", "Photos"};
static const char *const ACCENT_NAMES[ACCENT_COUNT] = {"Sky", "Mint", "Rose", "Violet", "Peach", "Sun"};
static const char *const COMP_NAMES[COMP_COUNT] = {"Battery", "Steps", "Weather", "Day", "Pet", "Agenda", "None"};
static const char *const TILE_NAMES[TILE_KINDS] = {"", "Music", "Activity", "Weather", "Timer", "Alarm", "Pet", "Agenda"};

static void brightness_changed(lv_event_t *e)
{
    CFG.brightness = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) svc_kv_save("cfg", &CFG, sizeof(CFG));
    else svc_brightness(CFG.brightness);
}

static void cycle_aod(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.aod_mode = (uint8_t)((CFG.aod_mode + 1) % AOD_COUNT);
    changed();
}

static void cycle_photo_minutes(lv_event_t *e)
{
    LV_UNUSED(e);
    static const uint8_t opts[] = {1, 5, 15, 60};
    size_t i = 0;
    while (i < sizeof(opts) && opts[i] != CFG.photo_minutes) i++;
    CFG.photo_minutes = opts[(i + 1) % sizeof(opts)];
    changed();
}

static void cycle_timeout(lv_event_t *e)
{
    LV_UNUSED(e);
    size_t i = 0;
    while (i < sizeof(TIMEOUTS) && TIMEOUTS[i] != CFG.timeout_s) i++;
    CFG.timeout_s = TIMEOUTS[(i + 1) % sizeof(TIMEOUTS)];
    changed();
}

static void cycle_left(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.comp_left = (uint8_t)((CFG.comp_left + 1) % COMP_COUNT);
    changed();
}

static void cycle_right(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.comp_right = (uint8_t)((CFG.comp_right + 1) % COMP_COUNT);
    changed();
}

static void cycle_accent(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.accent = (uint8_t)((CFG.accent + 1) % ACCENT_COUNT);
    changed();
}

static void open_faces(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(&app_faces);
}

static int tile_slot(int kind)
{
    for (int i = 0; i < TILE_SLOTS; i++) {
        if (CFG.tiles[i] == kind) return i;
    }
    return -1;
}

static void tile_toggle(lv_event_t *e)
{
    int kind = (int)(intptr_t)lv_event_get_user_data(e);
    int at = tile_slot(kind);
    if (at >= 0) {
        memmove(&CFG.tiles[at], &CFG.tiles[at + 1], (size_t)(TILE_SLOTS - at - 1));
        CFG.tiles[TILE_SLOTS - 1] = TILE_NONE;
    } else {
        int end = tile_slot(TILE_NONE);
        if (end >= 0) CFG.tiles[end] = (uint8_t)kind;
    }
    shell_rebuild_tiles();
    changed();
}

/* Which tiles sit beside the watch face, in the order they were switched on. */
static void tiles_page(void)
{
    page_title("Tiles");
    for (int kind = 1; kind < TILE_KINDS; kind++) {
        int at = tile_slot(kind);
        char value[8];
        snprintf(value, sizeof(value), at >= 0 ? "%d" : "Off", at + 1);
        row(NULL, TILE_NAMES[kind], value, at >= 0, tile_toggle, (void *)(intptr_t)kind);
    }
    note("Tap to switch a tile on or off. The number is its place beside the watch face.");
}

static void display_page(void)
{
    char buf[16];
    page_title("Display");
    slider_row(&ic_sun_28, CFG.brightness, 5, brightness_changed);
    row(&ic_clock_28, "Always-on", AOD_NAMES[CFG.aod_mode], CFG.aod_mode != AOD_OFF, cycle_aod, NULL);
    if (CFG.aod_mode == AOD_PHOTOS) {
        snprintf(buf, sizeof(buf), "%d min", CFG.photo_minutes);
        row(&ic_image_28, "Photo interval", buf, true, cycle_photo_minutes, NULL);
    }
    snprintf(buf, sizeof(buf), "%d s", CFG.timeout_s);
    row(&ic_timer_28, "Screen timeout", buf, true, cycle_timeout, NULL);
    row(&ic_steps_28, "Raise to wake", on_off(CFG.raise_to_wake), CFG.raise_to_wake, st_toggle, &CFG.raise_to_wake);
    row(&ic_face_28, "Watch face", FACE_NAMES[CFG.face], true, open_faces, NULL);
    row(NULL, "Left ring", COMP_NAMES[CFG.comp_left], true, cycle_left, NULL);
    row(NULL, "Right ring", COMP_NAMES[CFG.comp_right], true, cycle_right, NULL);
    row(&ic_spark_28, "Accent colour", ACCENT_NAMES[CFG.accent % ACCENT_COUNT], true, cycle_accent, NULL);
    row(&ic_grid_28, "Grid launcher", on_off(CFG.grid_launcher), CFG.grid_launcher, st_toggle, &CFG.grid_launcher);
    row(&ic_grid_28, "Tiles", NULL, false, st_open_page, (void *)tiles_page);
}

/* --------------------------------------------------------- sound and alerts */

static void volume_changed(lv_event_t *e)
{
    CFG.volume = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    svc_kv_save("cfg", &CFG, sizeof(CFG));
    svc_sound(SND_NOTIFY); /* so you hear the level you picked */
}

static void clear_alerts(lv_event_t *e)
{
    LV_UNUSED(e);
    for (int i = 0; i < S.notif_count; i++) svc_notif_dismiss(S.notifs[i].id);
    notif_clear();
    shell_toast("Notifications cleared");
}

static void sound_page(void)
{
    char buf[16];
    page_title("Sound & alerts");
    row(&ic_vol_up_28, "Sounds", on_off(CFG.sound), CFG.sound, st_toggle, &CFG.sound);
    slider_row(&ic_vol_down_28, CFG.volume, 0, volume_changed);
    row(&ic_moon_28, "Do not disturb", on_off(CFG.dnd), CFG.dnd, st_toggle, &CFG.dnd);
    row(&ic_zzz_28, "Bedtime mode", on_off(CFG.bedtime), CFG.bedtime, st_toggle, &CFG.bedtime);
    row(&ic_bell_28, "Show new alerts", on_off(CFG.previews), CFG.previews, st_toggle, &CFG.previews);
    snprintf(buf, sizeof(buf), "%d", S.notif_count);
    row(&ic_trash_28, "Clear notifications", buf, false, clear_alerts, NULL);
    note("Bedtime keeps the screen dark and silent until you press a button.");
}

/* ----------------------------------------------------------------- battery */

static void battery_page(void)
{
    char buf[32];
    page_title("Battery");
    if (S.batt.percent < 0) snprintf(buf, sizeof(buf), "Unknown");
    else snprintf(buf, sizeof(buf), "%d%%", S.batt.percent);
    const char *state = S.batt.charging ? "Charging"
                      : (S.batt.plugged ? (CFG.charge_limit && S.batt.percent >= 80 ? "Capped at 80%" : "Plugged in")
                                        : "On battery");
    row(&ic_bolt_28, state, buf, S.batt.charging, NULL, NULL);
    row(&ic_heart_28, "Charge limit 80%", on_off(CFG.charge_limit), CFG.charge_limit, st_toggle, &CFG.charge_limit);
    row(&ic_half_28, "Battery saver", on_off(CFG.saver), CFG.saver, st_toggle, &CFG.saver);
    row(&ic_clock_28, "Always-on", AOD_NAMES[CFG.aod_mode], CFG.aod_mode != AOD_OFF, cycle_aod, NULL);
    snprintf(buf, sizeof(buf), "%d s", CFG.timeout_s);
    row(&ic_timer_28, "Screen timeout", buf, true, cycle_timeout, NULL);
    note("Charge limit pauses charging at 80% to protect battery health. Battery saver dims the screen and turns off always-on and raise to wake.");
}

/* -------------------------------------------------------------------- apps */

static void cycle_goal(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.step_goal = (uint16_t)(CFG.step_goal >= 12000 ? 4000 : CFG.step_goal + 2000);
    changed();
}

static void web_hint(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_toast("Change this on the setup page");
}

static void clear_photos_yes(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_clear(CLEAR_PHOTOS);
}

static void clear_photos(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Delete all photos from the watch?", "Delete", clear_photos_yes);
}

static void clear_maps_yes(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_clear(CLEAR_MAPS);
    shell_toast("Offline maps deleted");
}

static void clear_maps(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Delete saved maps?", "Delete", clear_maps_yes);
}

static void pet_reset_yes(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_command("reset");
    shell_toast("A new egg is waiting");
}

static void pet_reset(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Start again with a new pet?", "New egg", pet_reset_yes);
}

static void apps_page(void)
{
    char buf[24];
    page_title("Apps");
    snprintf(buf, sizeof(buf), "%u", (unsigned)CFG.step_goal);
    row(&ic_steps_28, "Daily step goal", buf, true, cycle_goal, NULL);
    bool key = svc_configured(HAS_AI_KEY), cam = svc_configured(HAS_CAMERA);
    row(&ic_spark_28, "Assistant", key ? "Ready" : "No key", key, web_hint, NULL);
    row(&ic_camera_28, "Camera", cam ? "Saved" : "Not set", cam, web_hint, NULL);
    row(&ic_keys_28, "Keys buttons", "Edit", false, web_hint, NULL);
    snprintf(buf, sizeof(buf), "%d", svc_photo_count());
    row(&ic_image_28, "Delete photos", buf, false, clear_photos, NULL);
    row(&ic_pin_28, "Delete saved maps", NULL, false, clear_maps, NULL);
    row(&ic_paw_28, "New pet", NULL, false, pet_reset, NULL);
    row(&ic_phone_28, "Open setup page", NULL, false, st_open_page, (void *)st_phone_page);
}

/* ------------------------------------------------------------- date & time */

static void zone_text(char *buf, size_t len)
{
    int m = CFG.tz_minutes;
    snprintf(buf, len, "UTC%c%d:%02d", m < 0 ? '-' : '+', abs(m) / 60, abs(m) % 60);
}

static void zone_step(lv_event_t *e)
{
    int next = CFG.tz_minutes + (int)(intptr_t)lv_event_get_user_data(e);
    if (next < -12 * 60 || next > 14 * 60) return;
    CFG.tz_minutes = (int16_t)next;
    changed();
}

/* A live clock with the zone under it: step until the time looks right. */
void st_zone_controls(int y)
{
    char buf[24];
    struct tm t;
    svc_localtime(&t);
    fmt_clock(buf, sizeof(buf), &t, false);
    lv_obj_t *clock = ui_label(root, buf, &font_num_big, C_TEXT);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, y);
    zone_text(buf, sizeof(buf));
    lv_obj_align(ui_label(root, buf, F24, ui_accent()), LV_ALIGN_TOP_MID, 0, y + 112);
    lv_obj_t *back = ui_round_btn(root, 64, C_SURF2, &ic_minus_28, C_TEXT, zone_step, (void *)(intptr_t)-30);
    lv_obj_align(back, LV_ALIGN_TOP_MID, -128, y + 96);
    lv_obj_t *fwd = ui_round_btn(root, 64, C_SURF2, &ic_plus_28, C_TEXT, zone_step, (void *)(intptr_t)30);
    lv_obj_align(fwd, LV_ALIGN_TOP_MID, 128, y + 96);
}

static void zone_page(void)
{
    st_free_title("TIME ZONE", 40);
    st_zone_controls(84);
    st_free_text("Step in half hours until the time is right.", F16, C_DIM, 280);
}

static void clock_step(lv_event_t *e)
{
    svc_set_time(svc_time() + (int)(intptr_t)lv_event_get_user_data(e));
    st_refresh();
}

/* Setting the clock by hand, for when there is no phone or Wi-Fi to do it. */
static void clock_page(void)
{
    static const struct {
        const char *name;
        int step, y;
    } units[] = {{"Hour", 3600, 150}, {"Minute", 60, 222}, {"Day", 86400, 294}};
    char buf[64];
    struct tm t;
    svc_localtime(&t);
    st_free_title("SET THE CLOCK", 40);
    snprintf(buf, sizeof(buf), "%02d:%02d   %d/%d/%d", t.tm_hour, t.tm_min, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    st_free_text(buf, F28, C_TEXT, 78);
    for (size_t i = 0; i < 3; i++) {
        lv_obj_t *down = ui_round_btn(root, 60, C_SURF2, &ic_minus_28, C_TEXT, clock_step, (void *)(intptr_t)-units[i].step);
        lv_obj_align(down, LV_ALIGN_TOP_MID, -110, units[i].y);
        lv_obj_t *up = ui_round_btn(root, 60, C_SURF2, &ic_plus_28, C_TEXT, clock_step, (void *)(intptr_t)units[i].step);
        lv_obj_align(up, LV_ALIGN_TOP_MID, 110, units[i].y);
        lv_obj_align(ui_label(root, units[i].name, F20, C_DIM), LV_ALIGN_TOP_MID, 0, units[i].y + 18);
    }
}

static void time_page(void)
{
    char buf[24];
    page_title("Date & time");
    row(&ic_clock_28, "24-hour clock", on_off(CFG.clock_24h), CFG.clock_24h, st_toggle, &CFG.clock_24h);
    zone_text(buf, sizeof(buf));
    row(&ic_compass_28, "Time zone", buf, true, st_open_page, (void *)zone_page);
    row(&ic_timer_28, "Set the clock", svc_time_valid() ? NULL : "Not set", false, st_open_page, (void *)clock_page);
    note("The clock sets itself from your phone or Wi-Fi whenever it can.");
}

/* ------------------------------------------------------------------ system */

static void allow_update(lv_event_t *e)
{
    LV_UNUSED(e);
    char buf[48];
    snprintf(buf, sizeof(buf), "Upload within %d minutes", svc_update_allow() / 60);
    shell_toast(buf);
}

static void about_page(void)
{
    char buf[40];
    uint32_t total = 0, free_kb = 0, ram_in = 0, ram_ext = 0;
    svc_storage_info(&total, &free_kb);
    svc_memory_info(&ram_in, &ram_ext);
    page_title("About");
    row(&ic_orbit_28, "Orbit", ORBIT_VERSION, false, NULL, NULL);
    snprintf(buf, sizeof(buf), "%u.%u MB free", (unsigned)(free_kb / 1024), (unsigned)(free_kb % 1024 * 10 / 1024));
    row(NULL, "Storage", buf, false, NULL, NULL);
    snprintf(buf, sizeof(buf), "%u KB + %u.%u MB", (unsigned)ram_in, (unsigned)(ram_ext / 1024),
             (unsigned)(ram_ext % 1024 * 10 / 1024));
    row(NULL, "Free memory", buf, false, NULL, NULL);
    row(NULL, "Clock", svc_time_valid() ? "Set" : "Not set", false, NULL, NULL);
    note("Waveshare ESP32-S3 Touch AMOLED 1.75C");
}

static void do_restart(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_restart();
}

static void do_off(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_power_off();
}

static void do_erase(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_factory_reset();
}

static void ask_restart(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Restart Orbit?", "Restart", do_restart);
}

static void ask_off(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Turn the watch off?", "Power off", do_off);
}

static void ask_erase(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_confirm("Erase everything? Settings, pairings, photos and maps are deleted.", "Erase", do_erase);
}

static void wizard_start(lv_event_t *e);

static void system_page(void)
{
    page_title("System");
    row(&ic_info_28, "About", NULL, false, st_open_page, (void *)about_page);
    row(&ic_bolt_28, "Allow update", NULL, false, allow_update, NULL);
    row(&ic_spark_28, "Run setup again", NULL, false, wizard_start, NULL);
    row(&ic_restart_28, "Restart", NULL, false, ask_restart, NULL);
    row(&ic_power_28, "Power off", NULL, false, ask_off, NULL);
    row(&ic_trash_28, "Erase everything", NULL, false, ask_erase, NULL);
}

/* -------------------------------------------------------------------- root */

static void root_page(void)
{
    char buf[16];
    page_title("Settings");
    row(&ic_wifi_28, "Connectivity", S.wifi.connected ? "Online" : NULL, S.wifi.connected, st_open_page, (void *)connectivity_page);
    row(&ic_sun_28, "Display", NULL, false, st_open_page, (void *)display_page);
    row(&ic_bell_28, "Sound & alerts", CFG.dnd ? "Quiet" : NULL, false, st_open_page, (void *)sound_page);
    if (S.batt.percent >= 0) snprintf(buf, sizeof(buf), "%d%%", S.batt.percent);
    else buf[0] = 0;
    row(&ic_bolt_28, "Battery", buf, false, st_open_page, (void *)battery_page);
    row(&ic_grid_28, "Apps", NULL, false, st_open_page, (void *)apps_page);
    row(&ic_clock_28, "Date & time", NULL, false, st_open_page, (void *)time_page);
    row(&ic_gear_28, "System", NULL, false, st_open_page, (void *)system_page);
}

/* "Run setup again" from System. */
static void wizard_start(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(&app_welcome);
}

/* ---------------------------------------------------------------- the apps */

static void on_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_SETTINGS || e == EVT_WIFI || e == EVT_LINK || e == EVT_BATTERY || e == EVT_PHOTOS) st_refresh();
    /* Clocks on the time pages tick over with the minute. */
    if (e == EVT_MINUTE && !list) st_refresh();
}

static bool page_back(void)
{
    if (depth > 1) {
        pop_to(depth - 1);
        return true;
    }
    return wizard_back();
}

static void closed(lv_event_t *e)
{
    /* Settings and Setup share this file's state; when one hands over to the
     * other, the old screen is deleted after the new one has taken over. */
    if (lv_event_get_target_obj(e) != root) return;
    root = NULL, list = NULL, depth = 0, show_pending = false;
    /* Leaving setup half-way does not count as done: it returns next start. */
    wizard_reset();
}

static void begin(lv_obj_t *scr)
{
    root = scr;
    depth = 0;
    lv_obj_add_event_cb(scr, closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, on_event, NULL);
}

static page_fn pending_page;
extern const app_t app_settings;

void settings_open_page(const char *page)
{
    if (!page) pending_page = NULL;
    else if (strcmp(page, "wifi") == 0) pending_page = wifi_page;
    else if (strcmp(page, "bt") == 0) pending_page = bt_page;
    else if (strcmp(page, "display") == 0) pending_page = display_page;
    else if (strcmp(page, "sound") == 0) pending_page = sound_page;
    else if (strcmp(page, "battery") == 0) pending_page = battery_page;
    else if (strcmp(page, "tiles") == 0) pending_page = tiles_page;
    else pending_page = NULL;
    shell_open(&app_settings);
}

static void settings_create(lv_obj_t *scr)
{
    begin(scr);
    wizard_reset();
    push(root_page);
    if (pending_page) {
        page_fn p = pending_page;
        pending_page = NULL;
        push(p);
    }
}

static void welcome_create(lv_obj_t *scr)
{
    begin(scr);
    wizard_go(0);
}

const app_t app_settings = {
    .id = "settings", .name = "Settings", .icon = &ic_gear_48, .color = 0x969BA7,
    .create = settings_create, .back = page_back, .no_clock = true,
};

const app_t app_welcome = {
    .id = "welcome", .name = "Setup", .icon = &ic_spark_48, .color = 0xFFFFFF,
    .create = welcome_create, .back = page_back, .own_gestures = true, .no_clock = true,
};
