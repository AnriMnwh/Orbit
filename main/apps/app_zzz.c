/* ZZZ: a companion for Zenless Zone Zero.
 *
 * The app keeps track of your Battery Charge (it refills one point every six
 * minutes up to 240) and of the daily errands, and tells you when the charge
 * is full. The watch face is a wall of four old tube TVs showing the time,
 * with the charge underneath; like every face it also has a dim always-on
 * version. The look is our own take on the game's black-and-yellow street
 * style, drawn from scratch.
 */
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define ZZZ_COLOR 0xFFD60A
#define ZZZ_PAPER 0xF4F1DE /* warm white */
#define CAP 240
#define REGEN_S 360
#define ERRANDS 4

static struct {
    int64_t stamp;  /* when `charge` was right, UTC seconds; 0: not counting */
    int16_t charge;
    uint8_t told;   /* the "full" alert has been given */
    uint8_t done;   /* one bit per daily errand */
    int32_t day;    /* the game day `done` belongs to; game days start at 04:00 */
} Z;
static bool z_loaded;

static void zzz_load(void)
{
    if (z_loaded) return;
    z_loaded = true;
    if (!svc_kv_load("zzz", &Z, sizeof(Z))) memset(&Z, 0, sizeof(Z));
    if (Z.charge < 0 || Z.charge > CAP) Z.charge = 0, Z.stamp = 0;
}

/* The charge right now, and optionally how long until it is full. */
static int charge_now(int *seconds_left)
{
    zzz_load();
    int cur = Z.charge, into = 0;
    if (Z.stamp && svc_time_valid()) {
        int64_t gone = (int64_t)svc_time() - Z.stamp;
        if (gone < 0) gone = 0;
        int64_t n = Z.charge + gone / REGEN_S;
        cur = n > CAP ? CAP : (int)n;
        into = (int)(gone % REGEN_S);
    }
    if (seconds_left) *seconds_left = cur >= CAP ? 0 : (CAP - cur) * REGEN_S - into;
    return cur;
}

static void charge_add(int delta)
{
    int cur = charge_now(NULL), into = 0;
    time_t now = svc_time();
    /* Keep the minutes already counted towards the next point. */
    if (Z.stamp && cur < CAP) into = (int)(((int64_t)now - Z.stamp) % REGEN_S);
    cur += delta;
    cur = cur < 0 ? 0 : (cur > CAP ? CAP : cur);
    Z.charge = (int16_t)cur;
    Z.stamp = svc_time_valid() ? (int64_t)now - (into > 0 ? into : 0) : 0;
    Z.told = cur >= CAP;
    svc_kv_save("zzz", &Z, sizeof(Z));
    bus_emit(EVT_GAME, NULL);
}

void zzz_get(int *charge, int64_t *full, int *done)
{
    *charge = charge_now(NULL);
    /* Worked out from what was set, not from the clock, so it stays the same
     * number until something is spent or set. */
    *full = Z.stamp ? Z.stamp + (int64_t)(CAP - Z.charge) * REGEN_S : 0;
    *done = Z.done;
}

/* From the phone: set the charge, spend some, tick errands. A charge or
 * `done` below zero leaves that one as it is. */
void zzz_set(int charge, int spend, int done)
{
    zzz_load();
    if (charge >= 0) charge_add(charge - charge_now(NULL));
    if (spend > 0) charge_add(-spend);
    if (done >= 0 && (uint8_t)(done & ((1 << ERRANDS) - 1)) != Z.done) {
        Z.done = (uint8_t)(done & ((1 << ERRANDS) - 1));
        svc_kv_save("zzz", &Z, sizeof(Z));
        bus_emit(EVT_GAME, NULL);
    }
}

/* The errands start again at four in the morning. */
static void errands_roll(void)
{
    zzz_load();
    if (!svc_time_valid()) return;
    int32_t day = (int32_t)(((int64_t)svc_time() + CFG.tz_minutes * 60 - 4 * 3600) / 86400);
    if (day == Z.day) return;
    Z.day = day, Z.done = 0;
    svc_kv_save("zzz", &Z, sizeof(Z));
    bus_emit(EVT_GAME, NULL);
}

static void zzz_duty(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e != EVT_MINUTE) return;
    errands_roll();
    if (!Z.stamp || Z.told || charge_now(NULL) < CAP) return;
    Z.told = 1;
    svc_kv_save("zzz", &Z, sizeof(Z));
    /* Not worth waking anyone for. */
    if (CFG.dnd || CFG.bedtime || S.theatre) return;
    if (CFG.sound) svc_sound(SND_NOTIFY);
    shell_alert("Battery full", "Your charge is at 240.\nTime to spend some.");
}

/* Called once at boot, so the alert works whether or not the app is open. */
void zzz_init(void)
{
    zzz_load();
    bus_watch(lv_layer_top(), zzz_duty, NULL);
}

/* ---------------------------------------------------------------- TV wall */

#define TV_W 140
#define TV_H 112
#define TV_GAP 8

typedef struct {
    lv_obj_t *root, *screen[4], *digit[4];
    lv_obj_t *day, *date, *charge;
    bool ambient, home;
    int8_t noisy; /* which screen is showing static, or -1 */
    uint8_t hold; /* frames of static left */
    uint16_t wait;
    uint32_t rand;
} wall_t;

static uint32_t wall_rnd(wall_t *w)
{
    w->rand ^= w->rand << 13, w->rand ^= w->rand >> 17, w->rand ^= w->rand << 5;
    return w->rand;
}

static void wall_refresh(wall_t *w)
{
    static const char *const days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    struct tm t;
    svc_localtime(&t);
    int h = t.tm_hour;
    if (!CFG.clock_24h) h = h % 12 ? h % 12 : 12;
    char d[4][2] = {{(char)('0' + h / 10), 0}, {(char)('0' + h % 10), 0}, {(char)('0' + t.tm_min / 10), 0}, {(char)('0' + t.tm_min % 10), 0}};
    if (!CFG.clock_24h && h < 10) d[0][0] = 0; /* the first set stays blank */
    for (int i = 0; i < 4; i++) lv_label_set_text(w->digit[i], d[i]);

    char buf[24];
    lv_label_set_text(w->day, days[t.tm_wday % 7]);
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, months[t.tm_mon % 12]);
    lv_label_set_text(w->date, buf);

    int cur = charge_now(NULL);
    if (!Z.stamp) buf[0] = 0;
    else if (cur >= CAP) snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  FULL");
    else snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE "  %d / %d", cur, CAP);
    lv_label_set_text(w->charge, buf);
}

static void wall_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    if (e == EVT_MINUTE || e == EVT_SETTINGS || e == EVT_GAME || e == EVT_DISPLAY) wall_refresh(user);
}

/* Drawn over a screen and its number: scan lines, or a burst of static. */
static void screen_draw(lv_event_t *e)
{
    wall_t *w = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);

    if (w->noisy >= 0 && w->screen[w->noisy] == obj) {
        d.bg_color = lv_color_hex(0x101010);
        d.radius = 10;
        lv_draw_rect(layer, &d, &a);
        d.radius = 0;
        for (int y = a.y1 + 4; y + 6 <= a.y2 - 3; y += 7) {
            for (int x = a.x1 + 4; x + 9 <= a.x2 - 3; x += 10) {
                uint32_t r = wall_rnd(w);
                if (r & 1) continue;
                uint8_t g = (uint8_t)(60 + (r >> 8) % 180);
                d.bg_color = lv_color_make(g, g, (uint8_t)(g * 9 / 10));
                lv_area_t b = {x, y, x + 8, y + 5};
                lv_draw_rect(layer, &d, &b);
            }
        }
        return;
    }
    d.bg_color = lv_color_black();
    d.bg_opa = 56;
    for (int y = a.y1 + 5; y < a.y2 - 3; y += 4) {
        lv_area_t line = {a.x1 + 2, y, a.x2 - 2, y};
        lv_draw_rect(layer, &d, &line);
    }
}

/* Caution tape along two stretches of the edge. */
static void tape_draw(lv_event_t *e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = lv_color_hex(ZZZ_COLOR), d.width = 8, d.radius = 229;
    d.center.x = a.x1 + SCR_C, d.center.y = a.y1 + SCR_C;
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < 9; i++) {
            d.start_angle = (side ? 300 : 120) + i * 7;
            d.end_angle = d.start_angle + 4;
            lv_draw_arc(lv_event_get_layer(e), &d);
        }
    }
}

/* Every few seconds one set loses its picture for a moment. */
static void wall_tick(lv_timer_t *t)
{
    wall_t *w = lv_timer_get_user_data(t);
    if (S.display != DISPLAY_ACTIVE || !lv_obj_is_visible(w->root)) return;
    if (w->home && shell_current()) return;
    if (w->noisy >= 0) {
        lv_obj_invalidate(w->screen[w->noisy]);
        if (w->hold-- == 0) w->noisy = -1, w->wait = (uint16_t)(25 + wall_rnd(w) % 40);
    } else if (w->wait-- == 0) {
        w->noisy = (int8_t)(wall_rnd(w) % 4);
        w->hold = (uint8_t)(1 + wall_rnd(w) % 3);
        lv_obj_invalidate(w->screen[w->noisy]);
    }
}

static void wall_freed(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void tv_create(wall_t *w, int i)
{
    bool lit = i < 2; /* the hours glow yellow; the minutes are dark with pale numbers */
    lv_color_t yellow = lv_color_hex(ZZZ_COLOR);
    int col = i % 2, row = i / 2;
    lv_obj_t *tv = ui_box(w->root, TV_W, TV_H);
    lv_obj_remove_flag(tv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(tv, LV_ALIGN_CENTER, (col ? 1 : -1) * (TV_W + TV_GAP) / 2, (row ? 1 : -1) * (TV_H + TV_GAP) / 2 - 14);
    lv_obj_set_style_radius(tv, 14, 0);
    lv_obj_set_style_border_width(tv, 2, 0);
    lv_obj_set_style_border_color(tv, lv_color_hex(w->ambient ? 0x3A3A3A : 0x474747), 0);

    lv_obj_t *scr = ui_box(tv, 100, 94);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(scr, 9, 9);
    lv_obj_set_style_radius(scr, 10, 0);
    w->screen[i] = scr;

    lv_color_t ink = lit ? lv_color_black() : lv_color_hex(ZZZ_PAPER);
    if (w->ambient) {
        ink = lit ? ui_tint(yellow, 62) : lv_color_hex(0x8C8A80);
        lv_obj_set_style_border_width(scr, 1, 0);
        lv_obj_set_style_border_color(scr, lit ? ui_tint(yellow, 38) : lv_color_hex(0x333333), 0);
    } else {
        lv_obj_set_style_bg_color(tv, lv_color_hex(0x232323), 0);
        lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(scr, lit ? yellow : lv_color_hex(0x15150F), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(scr, screen_draw, LV_EVENT_DRAW_POST, w);
        /* Two dials and a power light down the side. */
        for (int k = 0; k < 3; k++) {
            lv_obj_t *dial = ui_box(tv, k < 2 ? 14 : 6, k < 2 ? 14 : 6);
            lv_obj_remove_flag(dial, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dial, k < 2 ? lv_color_hex(0x4E4E4E) : yellow, 0);
            lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
            lv_obj_set_pos(dial, k < 2 ? 116 : 120, k == 0 ? 16 : (k == 1 ? 40 : 90));
        }
    }
    w->digit[i] = ui_label(scr, "", w->ambient ? &font_num_big : &font_num_heavy, ink);
    lv_obj_center(w->digit[i]);
}

/* Builds the TV wall into `root`. `home` marks the live watch face, which
 * stays still while an app covers it. */
void zzz_face(lv_obj_t *root, bool ambient, bool home)
{
    wall_t *w = lv_malloc_zeroed(sizeof(wall_t));
    if (!w) return;
    lv_color_t yellow = lv_color_hex(ZZZ_COLOR);
    w->ambient = ambient, w->home = home, w->noisy = -1, w->wait = 30, w->rand = 0x51F15EEDu;
    w->root = ui_box(root, SCR, SCR);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->root, wall_freed, LV_EVENT_DELETE, w);
    if (!ambient) lv_obj_add_event_cb(w->root, tape_draw, LV_EVENT_DRAW_MAIN, NULL);

    /* The day on a yellow tag, the date beside it. */
    lv_obj_t *row = ui_box(w->root, LV_SIZE_CONTENT, 32);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 48);
    w->day = ui_label(row, "", F20, ambient ? ui_tint(yellow, 62) : lv_color_black());
    lv_obj_set_style_text_letter_space(w->day, 2, 0);
    if (!ambient) {
        lv_obj_set_style_bg_color(w->day, yellow, 0);
        lv_obj_set_style_bg_opa(w->day, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(w->day, 10, 0);
        lv_obj_set_style_pad_ver(w->day, 3, 0);
        lv_obj_set_style_radius(w->day, 5, 0);
    }
    w->date = ui_label(row, "", F20, ambient ? lv_color_hex(0x8C8A80) : lv_color_hex(ZZZ_PAPER));
    lv_obj_set_style_text_letter_space(w->date, 2, 0);

    for (int i = 0; i < 4; i++) tv_create(w, i);

    w->charge = ui_label(w->root, "", F24, ambient ? ui_tint(yellow, 62) : yellow);
    lv_obj_set_style_text_letter_space(w->charge, 1, 0);
    lv_obj_align(w->charge, LV_ALIGN_CENTER, 0, 128);

    bus_watch(w->root, wall_event, w);
    if (!ambient) ui_timer(w->root, wall_tick, 130, w);
    wall_refresh(w);
}

/* ------------------------------------------------------------------- app */

static lv_obj_t *zz_scr, *zz_num, *zz_arc, *zz_hint, *zz_errand[ERRANDS];

static void zzz_paint(void)
{
    if (!zz_scr) return;
    lv_color_t yellow = lv_color_hex(ZZZ_COLOR);
    char buf[72];
    int left, cur = charge_now(&left);
    snprintf(buf, sizeof(buf), "%d", cur);
    lv_label_set_text(zz_num, buf);
    ui_arc_set(zz_arc, cur * 100 / CAP);

    if (!Z.stamp) {
        snprintf(buf, sizeof(buf), "%s", svc_time_valid() ? "Set it to match the game" : "Starts counting once the clock is set");
    } else if (cur >= CAP) {
        snprintf(buf, sizeof(buf), "Full. Time to spend some");
    } else {
        time_t at = svc_time() + left + (time_t)CFG.tz_minutes * 60;
        struct tm *tp = gmtime(&at), t = {0};
        if (tp) t = *tp;
        char clock[12];
        fmt_clock(clock, sizeof(clock), &t, false);
        const char *half = fmt_ampm(&t);
        if (left >= 3600) snprintf(buf, sizeof(buf), "Full in %d h %d min, at %s%s%s", left / 3600, left % 3600 / 60, clock, half[0] ? " " : "", half);
        else snprintf(buf, sizeof(buf), "Full in %d min, at %s%s%s", (left + 59) / 60, clock, half[0] ? " " : "", half);
    }
    lv_label_set_text(zz_hint, buf);
    lv_obj_set_style_text_color(zz_hint, Z.stamp && cur >= CAP ? yellow : C_DIM, 0);

    for (int i = 0; i < ERRANDS; i++) {
        bool done = Z.done & (1 << i);
        lv_obj_set_style_bg_color(zz_errand[i], done ? yellow : C_SURF, 0);
        ui_icon_set(ui_btn_icon(zz_errand[i]), NULL, done ? lv_color_black() : C_DIM);
    }
}

static void zzz_spend(lv_event_t *e)
{
    charge_add((int)(intptr_t)lv_event_get_user_data(e));
    zzz_paint();
}

/* A tap moves the count by one, holding moves it by ten at a time. */
static void zzz_nudge(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    charge_add(lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED ? dir : dir * 10);
    zzz_paint();
}

static void zzz_errand(lv_event_t *e)
{
    static const char *const names[ERRANDS] = {"Coffee", "Scratch card", "Video store", "Daily 400"};
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    Z.done ^= (uint8_t)(1 << i);
    svc_kv_save("zzz", &Z, sizeof(Z));
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", names[i], Z.done & (1 << i) ? "done" : "to do");
    shell_toast(buf);
    bus_emit(EVT_GAME, NULL); /* repaints this screen too */
}

static void zzz_use(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.face = FACE_ZZZ;
    settings_changed();
    shell_toast("TV wall watch face on");
}

static void zzz_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_MINUTE || e == EVT_GAME) zzz_paint();
}

static void zzz_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    zz_scr = NULL;
}

static void zzz_create(lv_obj_t *scr)
{
    lv_color_t yellow = lv_color_hex(ZZZ_COLOR);
    errands_roll();
    zz_scr = scr;
    lv_obj_add_event_cb(scr, zzz_closed, LV_EVENT_DELETE, NULL);

    zz_arc = ui_arc(scr, 446, 8, C_SURF, yellow, 135, 270);
    lv_obj_center(zz_arc);

    lv_obj_t *title = ui_label(scr, "BATTERY CHARGE", F16, lv_color_black());
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_style_bg_color(title, yellow, 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title, 10, 0);
    lv_obj_set_style_pad_ver(title, 3, 0);
    lv_obj_set_style_radius(title, 5, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

    /* The number with "/ 240" tucked beside it, moving as one. */
    lv_obj_t *row = ui_box(scr, LV_SIZE_CONTENT, 92);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -92);
    zz_num = ui_label(row, "", &font_num_heavy, yellow);
    lv_obj_t *cap = ui_label(row, "/ 240", F24, C_DIM);
    lv_obj_set_style_margin_bottom(cap, 22, 0);

    zz_hint = ui_label_fit(scr, "", F16, C_DIM, 330);
    lv_obj_align(zz_hint, LV_ALIGN_CENTER, 0, -32);

    /* What things cost in the game, one tap each. */
    static const int cost[3] = {20, 40, 60};
    for (int i = 0; i < 3; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "-%d", cost[i]);
        lv_obj_t *b = ui_pill_btn(scr, buf, C_SURF2, lv_color_hex(ZZZ_PAPER), zzz_spend, (void *)(intptr_t)-cost[i]);
        lv_obj_set_size(b, 96, 48);
        lv_obj_set_style_pad_hor(b, 0, 0);
        lv_obj_align(b, LV_ALIGN_CENTER, (i - 1) * 106, 24);
    }

    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = ui_round_btn(scr, 52, C_SURF2, i ? &ic_plus_28 : &ic_minus_28, C_TEXT, NULL, NULL);
        lv_obj_add_event_cb(b, zzz_nudge, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)(i ? 1 : -1));
        lv_obj_add_event_cb(b, zzz_nudge, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)(i ? 1 : -1));
        lv_obj_align(b, LV_ALIGN_CENTER, i ? 78 : -78, 86);
    }
    lv_obj_t *use = ui_round_btn(scr, 52, C_SURF, &ic_tv_28, yellow, zzz_use, NULL);
    lv_obj_align(use, LV_ALIGN_CENTER, 0, 86);

    static const lv_image_dsc_t *const icons[ERRANDS] = {&ic_cup_28, &ic_ticket_28, &ic_tape_28, &ic_check_28};
    for (int i = 0; i < ERRANDS; i++) {
        zz_errand[i] = ui_round_btn(scr, 46, C_SURF, icons[i], C_DIM, zzz_errand, (void *)(intptr_t)i);
        lv_obj_align(zz_errand[i], LV_ALIGN_CENTER, (i * 2 - 3) * 29, 148);
    }

    bus_watch(scr, zzz_event, NULL);
    zzz_paint();
}

const app_t app_zzz = {
    .id = "zzz", .name = "ZZZ", .icon = &ic_tv_48, .color = ZZZ_COLOR, .create = zzz_create,
};

#ifdef ORBIT_SIM
void zzz_sim_preset(int charge, int done)
{
    zzz_load();
    Z.charge = (int16_t)charge, Z.stamp = (int64_t)svc_time() - 100, Z.told = 0, Z.done = (uint8_t)done;
    Z.day = (int32_t)(((int64_t)svc_time() + CFG.tz_minutes * 60 - 4 * 3600) / 86400);
    bus_emit(EVT_GAME, NULL);
}
#endif
