/* The first-boot setup: a few full-screen steps (Wi-Fi, time zone, phone,
 * look) that run as pages inside the Settings screen, so they share its
 * page stack and rebuild-on-change machinery (app_settings.c).
 */
#include <stdio.h>
#include <string.h>

#include "settings_priv.h"

#define WIZARD_STEPS 6
static int wizard = -1; /* current first-boot step, or -1 in plain Settings */
static lv_obj_t *root;  /* the setup screen, which Settings owns */
static bool wz_android;

void wizard_reset(void)
{
    wizard = -1;
}

/* Back inside setup steps to the previous step. */
bool wizard_back(void)
{
    if (wizard <= 0) return false;
    wizard_go(wizard - 1);
    return true;
}

static void wizard_next(lv_event_t *e)
{
    LV_UNUSED(e);
    wizard_go(wizard + 1);
}

/* The frame every step shares: a badge, a title, a line or two of text, and
 * progress dots along the bottom, the way phone setup screens do it. */
static void wz_frame(const lv_image_dsc_t *icon, lv_color_t color, const char *title, const char *text)
{
    lv_obj_t *badge = ui_round_btn(root, 76, ui_tint(color, 26), icon, color, NULL, NULL);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 36);
    st_free_text(title, F28, C_TEXT, 122);
    if (text) st_free_text(text, F16, C_DIM, 162);

    lv_obj_t *dots = ui_box(root, WIZARD_STEPS * 16, 10);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -14);
    for (int i = 0; i < WIZARD_STEPS; i++) {
        lv_obj_t *d = ui_box(dots, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_color(d, i == wizard ? ui_accent() : C_SURF2, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_pos(d, 4 + i * 16, 1);
    }
}

static lv_obj_t *wz_button(const char *text, bool primary, int y, lv_event_cb_t cb, void *arg)
{
    lv_obj_t *b = ui_pill_btn(root, text, primary ? ui_accent() : C_SURF2, primary ? lv_color_black() : C_TEXT, cb, arg);
    lv_obj_set_height(b, 52);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, y);
    return b;
}

static void wz_welcome(void)
{
    wz_frame(&ic_spark_48, ui_accent(), "Hi there", "Let's get your Orbit ready. It takes about a minute.");
    wz_button("Start", true, -70, wizard_next, NULL);
}

static void wz_wifi(void)
{
    char buf[80];
    lv_color_t c = lv_color_hex(0x58B6FF);
    if (S.wifi.connected) {
        snprintf(buf, sizeof(buf), "Connected to %s. The clock will set itself.", S.wifi.ssid);
        wz_frame(&ic_check_48, C_GOOD, "You're online", buf);
        wz_button("Next", true, -70, wizard_next, NULL);
        return;
    }
    if (S.wifi.joining) {
        snprintf(buf, sizeof(buf), "Joining %s...", st_pick_ssid());
        wz_frame(&ic_wifi_48, c, "One moment", buf);
        return;
    }
    wz_frame(&ic_wifi_48, c, "Connect to Wi-Fi",
             S.wifi.failed ? "That didn't work. Check the password and try again."
                           : "Used for the time, photos, maps and the assistant.");
    wz_button("Choose network", true, -126, st_choose_network, NULL);
    wz_button("Use my phone", false, -68, st_open_page, (void *)st_phone_page);
    lv_obj_t *skip = ui_label(root, "Skip", F16, C_DIM);
    lv_obj_add_flag(skip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(skip, 14);
    lv_obj_add_event_cb(skip, wizard_next, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_align(skip, LV_ALIGN_BOTTOM_MID, 0, -36);
}

static void wz_time(void)
{
    st_free_title("YOUR TIME ZONE", 40);
    st_zone_controls(74);
    st_free_text(svc_time_valid() ? "Step until the time matches your phone." : "The time appears once Wi-Fi or your phone connects.",
              F16, C_DIM, 262);
    lv_obj_t *h = ui_pill_btn(root, CFG.clock_24h ? "24-hour" : "12-hour", C_SURF2, C_TEXT, st_toggle, &CFG.clock_24h);
    lv_obj_set_height(h, 46);
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, -72, -62);
    lv_obj_t *n = ui_pill_btn(root, "Next", ui_accent(), lv_color_black(), wizard_next, NULL);
    lv_obj_set_height(n, 46);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 72, -62);
}

static void wz_phone_kind(lv_event_t *e)
{
    wz_android = lv_event_get_user_data(e) != NULL;
    st_refresh();
}

static void wz_phone(void)
{
    lv_color_t c = lv_color_hex(0x4ADE9B);
    if (S.link.status == LINK_CONNECTED && !S.link.keys_mode) {
        wz_frame(&ic_check_48, C_GOOD, "Phone connected", S.link.name[0] ? S.link.name : "Notifications and music are on their way.");
        wz_button("Next", true, -70, wizard_next, NULL);
        return;
    }
    wz_frame(&ic_phone_48, c, "Pair your phone",
             wz_android ? "Install Gadgetbridge, tap +, and add Orbit as a Bangle.js."
                        : "Open Settings > Bluetooth on your iPhone and tap Orbit.");
    lv_obj_t *a = ui_pill_btn(root, "iPhone", wz_android ? C_SURF2 : c, wz_android ? C_TEXT : lv_color_black(), wz_phone_kind, NULL);
    lv_obj_set_height(a, 46);
    lv_obj_align(a, LV_ALIGN_BOTTOM_MID, -74, -126);
    lv_obj_t *b = ui_pill_btn(root, "Android", wz_android ? c : C_SURF2, wz_android ? lv_color_black() : C_TEXT, wz_phone_kind,
                              (void *)1);
    lv_obj_set_height(b, 46);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 74, -126);
    wz_button("Later", false, -62, wizard_next, NULL);
}

static void wz_accent(lv_event_t *e)
{
    CFG.accent = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_changed(); /* comes back round as EVT_SETTINGS and rebuilds the page */
}

static void wz_face(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.face = (uint8_t)((CFG.face + 1) % FACE_COUNT);
    settings_changed(); /* comes back round as EVT_SETTINGS and rebuilds the page */
}

static void wz_style(void)
{
    char buf[24];
    wz_frame(&ic_face_48, ui_accent(), "Make it yours", "Pick a colour and a watch face. Both can change later.");
    for (int i = 0; i < ACCENT_COUNT; i++) {
        lv_obj_t *s = ui_round_btn(root, 46, ui_accent_at(i), i == CFG.accent ? &ic_check_28 : NULL, lv_color_black(), wz_accent,
                                   (void *)(uintptr_t)i);
        lv_obj_align(s, LV_ALIGN_TOP_MID, (i - 2) * 56 - 28, 222);
    }
    snprintf(buf, sizeof(buf), "Face: %s", FACE_NAMES[CFG.face]);
    lv_obj_t *f = ui_pill_btn(root, buf, C_SURF2, C_TEXT, wz_face, NULL);
    lv_obj_set_height(f, 46);
    lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, -124);
    wz_button("Next", true, -62, wizard_next, NULL);
}

static void wizard_finish(lv_event_t *e)
{
    LV_UNUSED(e);
    wizard = -1;
    CFG.setup_done = 1;
    settings_changed();
    shell_home();
}

static void wz_tips(void)
{
    st_free_text("You're all set", F28, C_TEXT, 50);
    st_free_text("Swipe down for quick settings\nSwipe up for notifications\nSwipe sideways for tiles\n"
              "Side button opens apps\nHold it to talk to the assistant",
              F20, C_DIM, 100);
    wz_button("Done", true, -62, wizard_finish, NULL);
    lv_obj_t *dots = ui_label(root, "Everything here is also in Settings.", F14, C_FAINT);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void wizard_go(int step)
{
    static const page_fn steps[WIZARD_STEPS] = {wz_welcome, wz_wifi, wz_time, wz_phone, wz_style, wz_tips};
    if (step >= WIZARD_STEPS) step = WIZARD_STEPS - 1;
    wizard = step;
    root = st_root();
    st_run_page(steps[step]);
}

#ifdef ORBIT_SIM
void wizard_sim_step(int step)
{
    wizard_go(step);
}
#endif

