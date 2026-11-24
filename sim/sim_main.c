/* Renders Orbit screens to raw RGB565 files, one per scenario.
 *
 *   orbit_sim <data_dir> <out_dir> [name-filter]
 *
 * Time is simulated, so runs are deterministic and take a second or two.
 * tools/sim.ps1 builds this, runs it and turns the output into PNGs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

uint32_t sim_now_ms;
const char *sim_data_dir = ".";
static const char *out_dir = ".";
static const char *filter;

static uint16_t fb[SCR * SCR];
static struct {
    int x, y;
    bool down;
} touch;

static uint32_t tick_cb(void)
{
    return sim_now_ms;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = touch.x;
    data->point.y = touch.y;
    data->state = touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sim_run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        sim_now_ms += 5;
        lv_timer_handler();
    }
}

void sim_tap(int x, int y)
{
    touch.x = x, touch.y = y, touch.down = true;
    sim_run(60);
    touch.down = false;
    sim_run(120);
}

void sim_swipe(int x0, int y0, int x1, int y1, uint32_t ms)
{
    touch.x = x0, touch.y = y0, touch.down = true;
    sim_run(30);
    for (uint32_t t = 0; t <= ms; t += 10) {
        touch.x = x0 + (int)((x1 - x0) * (int)t / (int)ms);
        touch.y = y0 + (int)((y1 - y0) * (int)t / (int)ms);
        sim_run(10);
    }
    touch.down = false;
    sim_run(600);
}

static void shot(const char *name)
{
    if (filter && !strstr(name, filter)) return;
    lv_display_trigger_activity(NULL);
    sim_run(40);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    char path[600];
    snprintf(path, sizeof(path), "%s/%s.rgb565", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("cannot write %s\n", path);
        return;
    }
    fwrite(fb, 2, SCR * SCR, f);
    fclose(f);
    printf("shot %s\n", name);
}

static void settle(void)
{
    lv_display_trigger_activity(NULL);
    sim_run(700);
}

static void add_notif(uint32_t id, const char *app, const char *title, const char *body, int age_s)
{
    notif_t n = {.id = id, .when = svc_time() - age_s};
    snprintf(n.app, sizeof(n.app), "%s", app);
    snprintf(n.title, sizeof(n.title), "%s", title);
    snprintf(n.body, sizeof(n.body), "%s", body);
    notif_push(&n);
}

static void set_face(int face)
{
    CFG.face = (uint8_t)face;
    settings_changed();
    settle();
}

static void open_app(const char *id)
{
    shell_home();
    settle();
    shell_open(app_find(id));
    settle();
}

static void scenarios(void)
{
    CFG.dnd = 1; /* keep the sample notifications from popping up yet */
    add_notif(1, "MESSAGES", "Sam Rivera", "Running ten minutes late, save me a seat by the window?", 7200);
    add_notif(2, "CALENDAR", "Design review", "Starts in 15 minutes  -  Room 4", 900);
    add_notif(3, "MAIL", "Your order has shipped", "Arriving Thursday. Track your parcel in the app.", 120);
    CFG.dnd = 0;
    settle();

    shot("01_face_digital");
    set_face(FACE_ANALOG);
    shot("02_face_analog");
    set_face(FACE_PHOTO);
    shot("03_face_photo");
    set_face(FACE_DIGITAL);

    /* A running timer earns a chip on the watch face. */
    timer_start(272);
    sim_run(1200);
    shot("03b_face_ongoing");
    open_app("faces");
    shot("03c_face_picker");
    shell_home();
    settle();

    /* Tiles to the right of the face, in their configured order. */
    static const char *const tile_names[] = {"04_tile_activity", "04b_tile_weather", "04c_tile_timer"};
    for (int i = 0; i < 3; i++) {
        shell_show_tile(2 + i, 1);
        settle();
        shot(tile_names[i]);
    }
    shell_show_tile(1, 1);
    settle();
    shell_button(BTN_SIDE, PRESS_SHORT); /* side button opens the app list */
    settle();
    shot("05_launcher");
    sim_swipe(233, 330, 233, 110, 260);
    shot("05b_launcher_scrolled");
    shell_home();
    settle();

    shell_show_tile(1, 0);
    settle();
    shot("06_quick");
    shell_show_tile(1, 1);
    settle();
    shell_show_tile(1, 2);
    settle();
    shot("07_notifications");
    shell_show_tile(1, 1);
    settle();
    shell_show_tile(0, 1);
    settle();
    shot("08_glance");
    shell_show_tile(1, 1);
    settle();

    add_notif(4, "MESSAGES", "Alex Chen", "Are we still on for the hike on Saturday? I can drive.", 0);
    sim_run(300);
    shot("09_notification");
    shell_back();
    /* The same notification opened in full, as an Android phone offers it. */
    S.link.peer = PEER_ANDROID;
    shell_notification(&S.notifs[0]);
    sim_run(300);
    shot("09b_notification_open");
    shell_back();
    S.link.peer = PEER_IPHONE;

    snprintf(S.call.who, sizeof(S.call.who), "Jordan Lee");
    S.call.active = true;
    bus_emit(EVT_CALL, NULL);
    sim_run(200);
    shot("10_call");
    svc_call(false);

    shell_button(BTN_POWER, PRESS_LONG);
    sim_run(200);
    shot("11_power");
    shell_back();

    CFG.aod_mode = AOD_CLOCK;
    shell_rest();
    sim_run(200);
    shot("12_aod_clock");
    shell_wake();
    set_face(FACE_ANALOG);
    S.batt.plugged = true, S.batt.charging = true;
    shell_rest();
    sim_run(200);
    shot("12b_aod_analog_charging");
    shell_wake();
    S.batt.plugged = false, S.batt.charging = false;
    set_face(FACE_DIGITAL);
    CFG.aod_mode = AOD_PHOTOS;
    shell_rest();
    sim_run(200);
    shot("13_aod_photos");
    shell_wake();
    settle();

    /* Assistant: one shot per state, after the glow has eased in. */
    open_app("agent");
    sim_run(1500);
    shot("20_agent_idle");
    S.agent.status = AGENT_LISTENING, S.agent.level = 0.7f;
    bus_emit(EVT_AGENT, NULL);
    sim_run(1500);
    shot("21_agent_listening");
    S.agent.status = AGENT_THINKING, S.agent.level = 0;
    snprintf(S.agent.heard, sizeof(S.agent.heard), "What's a good shutter speed for waterfalls?");
    bus_emit(EVT_AGENT, NULL);
    sim_run(1500);
    shot("22_agent_thinking");
    S.agent.status = AGENT_SPEAKING, S.agent.level = 0.5f;
    snprintf(S.agent.reply, sizeof(S.agent.reply),
             "Try half a second to two seconds on a tripod for silky water. Use a low ISO and stop down to f/11.");
    bus_emit(EVT_AGENT, NULL);
    sim_run(1500);
    shot("23_agent_speaking");
    S.agent.status = AGENT_ERROR, S.agent.level = 0;
    snprintf(S.agent.error, sizeof(S.agent.error), "No Wi-Fi. Set it up in Settings.");
    bus_emit(EVT_AGENT, NULL);
    sim_run(1500);
    shot("24_agent_error");
    S.agent.status = AGENT_IDLE, S.agent.reply[0] = 0;

    open_app("music");
    shot("25_music");
    /* The same screen as the Orbit phone app drives it: picture and extras. */
    S.link.app = true, S.link.peer = PEER_ANDROID;
    S.media.has_art = true, S.media.shuffle = true;
    S.media.can_seek = S.media.can_shuffle = S.media.can_repeat = S.media.can_like = true;
    snprintf(S.media.app, sizeof(S.media.app), "Spotify");
    bus_emit(EVT_MEDIA, NULL);
    sim_run(200);
    shot("25b_music_app");
    S.link.app = false, S.link.peer = PEER_IPHONE;
    S.media.has_art = false, S.media.shuffle = false, S.media.app[0] = 0;
    S.media.can_seek = S.media.can_shuffle = S.media.can_repeat = S.media.can_like = false;

    open_app("keys");
    shot("26_keys");
    sim_swipe(233, 330, 233, 90, 240);
    shot("27_keys_page2");
    shell_home();
    settle();

    open_app("phone");
    shot("28_phone");
    S.link.status = LINK_WAITING, S.link.peer = PEER_NONE;
    bus_emit(EVT_LINK, NULL);
    sim_run(100);
    shot("29_phone_unpaired");
    S.link.status = LINK_CONNECTED, S.link.peer = PEER_IPHONE;
    bus_emit(EVT_LINK, NULL);

    open_app("maps");
    shot("30_maps");

    open_app("camera");
    shot("31_camera_off");
    svc_camera_link(true);
    sim_run(100);
    shot("32_camera_ready");
    S.cam.recording = true;
    bus_emit(EVT_CAMERA, NULL);
    sim_run(100);
    shot("33_camera_recording");
    S.cam.recording = false;

    open_app("photos");
    shot("34_photos");

    extern void pet_sim_preset(int mood);
    open_app("pet");
    shot("35_pet_egg");
    pet_sim_preset(0);
    open_app("pet");
    shot("36_pet_happy");
    pet_sim_preset(1);
    open_app("pet");
    shot("37_pet_hungry");
    pet_sim_preset(2);
    open_app("pet");
    shot("38_pet_asleep");

    open_app("timer");
    shot("39_timer");
    sim_tap(233, 321);
    sim_run(41000);
    shot("40_timer_running");

    extern void more_sim_preset(void);
    more_sim_preset();
    open_app("stopwatch");
    shot("40b_stopwatch");
    open_app("alarm");
    shot("45_alarm");
    open_app("fitness");
    shot("46_fitness");
    open_app("weather");
    shot("47_weather");
    open_app("calc");
    sim_tap(134, 227), sim_tap(200, 293), sim_tap(332, 293), sim_tap(266, 161);
    shot("49_calculator");
    shell_home();
    settle();
    shell_show_tile(3, 1);
    settle();
    shot("50_tile_weather");
    shell_show_tile(1, 1);
    settle();

    /* The newer apps, one shot each. */
    static const char *const more[] = {"find", "sun", "exposure", "workout", "presenter"};
    double place[2] = {-33.8568, 151.2153};
    S.gps.lat = place[0], S.gps.lon = place[1], S.gps.fix = true;
    CFG.tz_minutes = 660;
    for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); i++) {
        char name[40];
        snprintf(name, sizeof(name), "%02d_%s", 60 + (int)i, more[i]);
        open_app(more[i]);
        shot(name);
    }
    CFG.tz_minutes = 0;
    svc_link_keys_mode(false);
    shell_home();
    settle();

    /* What the Orbit phone app adds: the calendar, the phone's battery and
     * the user's own quick replies. */
    open_app("agenda");
    shot("74_agenda_empty");
    time_t t0 = svc_time();
    S.agenda_known = true, S.agenda_count = 4;
    S.agenda[0] = (agenda_item_t){.start = t0 - 600, .end = t0 + 1800, .title = "Design review", .place = "Room 4"};
    S.agenda[1] = (agenda_item_t){.start = t0 + 3 * 3600, .end = t0 + 4 * 3600, .title = "Lunch with Sam", .place = "Harbour Kitchen"};
    S.agenda[2] = (agenda_item_t){.start = (t0 / 86400 + 1) * 86400, .end = (t0 / 86400 + 2) * 86400, .all_day = true,
                                  .title = "Mum's birthday"};
    S.agenda[3] = (agenda_item_t){.start = t0 + 26 * 3600, .end = t0 + 27 * 3600 + 1800,
                                  .title = "Band practice and gear load-in at the hall", .place = "Community Hall"};
    bus_emit(EVT_AGENDA, NULL);
    sim_run(200);
    shot("75_agenda");
    sim_swipe(233, 340, 233, 110, 260);
    shot("75b_agenda_scrolled");
    /* All-day entries follow the local date. Eleven hours west of UTC it is
     * still yesterday evening, so an all-day entry dated yesterday must stay
     * up as today's, although the UTC midnight that ends it has passed. */
    agenda_item_t keep[AGENDA_MAX];
    memcpy(keep, S.agenda, sizeof(keep));
    S.agenda[0] = (agenda_item_t){.start = (t0 / 86400 - 1) * 86400, .end = (t0 / 86400) * 86400, .all_day = true,
                                  .title = "Mum's birthday"};
    S.agenda[1] = keep[1];
    S.agenda_count = 2;
    CFG.tz_minutes = -660;
    settings_changed();
    sim_run(200);
    shot("75c_agenda_allday_west");
    CFG.tz_minutes = 0;
    memcpy(S.agenda, keep, sizeof(keep));
    S.agenda_count = 4;
    settings_changed();
    sim_run(200);
    shell_home();
    settle();
    CFG.tiles[0] = TILE_AGENDA;
    shell_rebuild_tiles();
    settle();
    shell_show_tile(0, 1);
    settle();
    shot("76_tile_agenda");
    CFG.tiles[0] = TILE_MUSIC;
    shell_rebuild_tiles();
    settle();

    S.link.peer = PEER_ANDROID, S.link.app = true;
    snprintf(S.link.name, sizeof(S.link.name), "Pixel 9");
    S.phone.known = true, S.phone.percent = 80, S.phone.charging = true;
    open_app("phone");
    shot("77_phone_battery");
    S.reply_count = 3;
    snprintf(S.replies[0], REPLY_LEN, "On it");
    snprintf(S.replies[1], REPLY_LEN, "Give me ten minutes");
    snprintf(S.replies[2], REPLY_LEN, "Call you after the shoot");
    shell_notification(&S.notifs[0]);
    sim_run(300);
    sim_swipe(233, 340, 233, 120, 260);
    shot("78_replies_own");
    shell_back();
    S.reply_count = 0, S.phone.known = false, S.link.app = false, S.link.peer = PEER_IPHONE;
    shell_home();
    settle();

    /* Settings: the category list, then into Display, Wi-Fi and the keyboard. */
    open_app("settings");
    shot("41_settings");
    sim_tap(233, 245); /* Display, the second row */
    sim_run(300);
    shot("42_settings_display");
    shell_back();
    sim_run(300);
    sim_tap(233, 163); /* Connectivity */
    sim_run(200);
    sim_tap(233, 163); /* Wi-Fi */
    sim_run(200);
    shot("43_settings_wifi");
    sim_tap(233, 327); /* Choose network */
    sim_run(300);
    shot("43b_settings_networks");
    sim_tap(233, 163); /* the first, password-protected network */
    sim_run(300);
    sim_tap(37, 177), sim_tap(124, 177), sim_tap(300, 177); /* type a few letters */
    shot("43c_settings_keyboard");
    shell_home();
    settle();

    /* First-boot setup, step by step. */
    S.wifi.connected = false, S.wifi.joining = false;
    open_app("welcome");
    shot("80_setup_welcome");
    static const char *const steps[] = {"81_setup_wifi", "82_setup_time", "83_setup_phone", "84_setup_style", "85_setup_done"};
    for (int i = 0; i < 5; i++) {
        extern void wizard_sim_step(int step);
        wizard_sim_step(i + 1);
        sim_run(200);
        shot(steps[i]);
    }
    S.wifi.connected = true;
    shell_home();
    settle();

    open_app("torch");
    shot("44_torch");
    shell_home();
    settle();

    open_app("faces");
    sim_run(300);
    shot("x24_face_picker");
    shell_home();
    set_face(FACE_DIGITAL);

    open_app("world");
    shot("x30_world");
    /* A month of entries from the phone: the Agenda list, the month view's
     * dots, and a day opened in Calendar before and after the phone answers. */
    static const struct {
        int day, hour, mins;
        const char *title, *place;
    } plan[] = {
        {0, 13, 60, "Lunch with Sam", "Harbour Kitchen"}, {0, 16, 30, "Call the lab", ""},
        {1, 6, 120, "Sunrise shoot", "North Bondi"},      {2, 19, 120, "Band practice", "Studio 3"},
        {2, 21, 60, "Pick up strings", ""},               {4, 10, 45, "Dentist", "High St"},
        {7, 18, 150, "Mum's birthday dinner", "Home"},    {9, 9, 480, "Wedding: Lee and Ana", "Centennial Park"},
    };
    time_t day0 = svc_time() / 86400 * 86400;
    S.agenda_known = true, S.agenda_count = 0;
    for (int k = 0; k < 26; k++) {
        int j = k % 8, week = k / 8;
        agenda_item_t *it = &S.agenda[S.agenda_count++];
        memset(it, 0, sizeof(*it));
        it->start = day0 + (time_t)(plan[j].day + week * 10) * 86400 + plan[j].hour * 3600;
        it->end = it->start + plan[j].mins * 60;
        snprintf(it->title, sizeof(it->title), "%s", plan[j].title);
        snprintf(it->place, sizeof(it->place), "%s", plan[j].place);
    }
    S.busy[0].year = 2026, S.busy[0].month = 11, S.busy[0].days = (1u << 3) | (1u << 5) | (1u << 6);
    bus_emit(EVT_AGENDA, NULL);
    open_app("agenda");
    shot("x35_agenda_month");
    for (int i = 0; i < 12; i++) sim_swipe(233, 360, 233, 60, 120);
    shot("x35b_agenda_more");

    open_app("calendar");
    shot("x31_calendar");
    sim_tap(233, 206); /* Thursday the 12th */
    sim_run(300);
    shot("x31b_calendar_day");
    S.day.count = 3, S.day.known = true;
    S.day.items[0] = (agenda_item_t){.start = day0 + 2 * 86400, .end = day0 + 3 * 86400, .all_day = true, .title = "Rates due"};
    S.day.items[1] = S.agenda[3];
    S.day.items[2] = S.agenda[4];
    bus_emit(EVT_AGENDA, NULL);
    sim_run(300);
    shot("x31c_calendar_day_phone");
    shell_back();
    sim_run(300);
    sim_tap(277, 132); /* Saturday the 7th: past, and not asked of any phone */
    sim_run(300);
    shot("x31d_calendar_day_unknown");
    shell_back();
    sim_run(300);
    open_app("moon");
    shot("x32_moon");
    extern void extra_sim_moon(double days);
    extra_sim_moon(10.5);
    shot("x32b_moon_gibbous");
    extra_sim_moon(22.0);
    shot("x32c_moon_waning");
    extra_sim_moon(0);
    shell_home();
}

int main(int argc, char **argv)
{
    if (argc > 1) sim_data_dir = argv[1];
    if (argc > 2) out_dir = argv[2];
    if (argc > 3) filter = argv[3];
    setvbuf(stdout, NULL, _IONBF, 0);

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(SCR, SCR);
    lv_display_set_buffers(disp, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    sim_services_init();
    orbit_boot();
    CFG.timeout_s = 250; /* scenarios decide when the display rests */
    sim_run(500);
    shot("00_boot");
    CFG.setup_done = 1;  /* the welcome gets its own shot below */
    sim_run(1500);
    scenarios();
    return 0;
}
