/* World Clock, Calendar, Moon and Marble. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/ui.h"

/* Days since 1970-01-01 for a calendar date (month 1..12). */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* 0 = Sunday. */
static int weekday(int y, int m, int d)
{
    return (int)(((days_from_civil(y, m, d) % 7) + 11) % 7);
}

static int month_days(int y, int m)
{
    static const int8_t len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : len[m - 1];
}

static struct tm tm_of(time_t t)
{
    struct tm out = {0};
    struct tm *p = gmtime(&t);
    if (p) out = *p;
    return out;
}

/* ------------------------------------------------------------ world clock */

#define WORLD_COLOR 0x7FD1FF

enum { DST_NONE, DST_EU, DST_US, DST_AU, DST_NZ };
typedef struct {
    const char *name;
    int16_t offset; /* standard time, minutes east of UTC */
    uint8_t dst;
} city_t;

static const city_t CITIES[] = {
    {"Honolulu", -600, DST_NONE}, {"Los Angeles", -480, DST_US}, {"Denver", -420, DST_US},  {"Chicago", -360, DST_US},
    {"New York", -300, DST_US},   {"Toronto", -300, DST_US},     {"Sao Paulo", -180, DST_NONE}, {"London", 0, DST_EU},
    {"Paris", 60, DST_EU},        {"Berlin", 60, DST_EU},        {"Athens", 120, DST_EU},   {"Johannesburg", 120, DST_NONE},
    {"Istanbul", 180, DST_NONE},  {"Nairobi", 180, DST_NONE},    {"Dubai", 240, DST_NONE},  {"Mumbai", 330, DST_NONE},
    {"Bangkok", 420, DST_NONE},   {"Singapore", 480, DST_NONE},  {"Hong Kong", 480, DST_NONE}, {"Shanghai", 480, DST_NONE},
    {"Seoul", 540, DST_NONE},     {"Tokyo", 540, DST_NONE},      {"Sydney", 600, DST_AU},   {"Auckland", 720, DST_NZ},
};
#define CITY_COUNT ((int)(sizeof(CITIES) / sizeof(CITIES[0])))

static uint32_t wc_chosen = (1u << 4) | (1u << 7) | (1u << 21); /* New York, London, Tokyo */
static bool wc_loaded;
static lv_obj_t *wc_list;
typedef struct {
    int city;
    lv_obj_t *day, *time, *half; /* half is NULL on a 24-hour clock */
} wc_card_t;
static wc_card_t wc_cards[24]; /* one per chosen city, at most CITY_COUNT */
static int wc_cards_n;

/* Day of the month of the nth Sunday (nth = 0: the last one). */
static int sunday(int y, int m, int nth)
{
    int first = 1 + (7 - weekday(y, m, 1)) % 7;
    if (nth) return first + 7 * (nth - 1);
    int last = first + 28;
    return last > month_days(y, m) ? last - 7 : last;
}

/* A local wall-clock moment as UTC seconds, for a zone `offset` minutes east. */
static time_t moment(int y, int m, int d, int hour, int offset)
{
    return (time_t)days_from_civil(y, m, d) * 86400 + hour * 3600 - (time_t)offset * 60;
}

/* Minutes east of UTC in that city right now, summer time included. */
static int city_offset(const city_t *c, time_t now)
{
    int y = tm_of(now).tm_year + 1900, std = c->offset;
    bool summer = false;
    switch (c->dst) {
    case DST_EU: /* last Sunday of March to last Sunday of October, changing at 01:00 UTC */
        summer = now >= moment(y, 3, sunday(y, 3, 0), 1, 0) && now < moment(y, 10, sunday(y, 10, 0), 1, 0);
        break;
    case DST_US: /* second Sunday of March to first Sunday of November, at 2 am local */
        summer = now >= moment(y, 3, sunday(y, 3, 2), 2, std) && now < moment(y, 11, sunday(y, 11, 1), 2, std + 60);
        break;
    case DST_AU: /* first Sunday of October to first Sunday of April */
        summer = now >= moment(y, 10, sunday(y, 10, 1), 2, std) || now < moment(y, 4, sunday(y, 4, 1), 3, std + 60);
        break;
    case DST_NZ: /* last Sunday of September to first Sunday of April */
        summer = now >= moment(y, 9, sunday(y, 9, 0), 2, std) || now < moment(y, 4, sunday(y, 4, 1), 3, std + 60);
        break;
    default: break;
    }
    return std + (summer ? 60 : 0);
}

static void world_build(void);
static void world_tick(void);

static void world_rebuild(void *unused)
{
    LV_UNUSED(unused);
    if (wc_list) world_build();
}

/* Tap a city in the list to add it; hold one of yours to take it off. */
static void world_toggle(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    wc_chosen ^= 1u << i;
    svc_kv_save("world", &wc_chosen, sizeof(wc_chosen));
    if (!(wc_chosen & (1u << i))) shell_toast("Removed");
    lv_async_call(world_rebuild, NULL); /* not from inside the row's own event */
}

/* The two texts on a city card that change with time: the day line and the clock. */
static void world_texts(int i, time_t now, char *day_line, size_t day_len, char *clock, size_t clock_len, const char **half)
{
    long today = (long)((now + (time_t)CFG.tz_minutes * 60) / 86400);
    int off = city_offset(&CITIES[i], now);
    time_t there = now + (time_t)off * 60;
    struct tm t = tm_of(there);
    long day = (long)(there / 86400);
    int diff = off - CFG.tz_minutes;
    const char *when = day > today ? "Tomorrow" : (day < today ? "Yesterday" : "Today");
    if (diff == 0) snprintf(day_line, day_len, "%s, same time", when);
    else if (diff % 60 == 0) snprintf(day_line, day_len, "%s, %+d h", when, diff / 60);
    else snprintf(day_line, day_len, "%s, %+d:%02d h", when, diff / 60, abs(diff) % 60);
    fmt_clock(clock, clock_len, &t, false);
    *half = fmt_ampm(&t);
}

/* Once a minute only these labels change, so the cards stay and just get new text. */
static void world_tick(void)
{
    time_t now = svc_time();
    for (int k = 0; k < wc_cards_n; k++) {
        char day_line[48], clock[12];
        const char *half;
        world_texts(wc_cards[k].city, now, day_line, sizeof(day_line), clock, sizeof(clock), &half);
        ui_label_set(wc_cards[k].day, day_line);
        ui_label_set(wc_cards[k].time, clock);
        if (wc_cards[k].half) ui_label_set(wc_cards[k].half, half);
    }
}

static void world_build(void)
{
    lv_color_t c = lv_color_hex(WORLD_COLOR);
    time_t now = svc_time();
    int32_t keep = lv_obj_get_scroll_y(wc_list);
    lv_obj_clean(wc_list);
    wc_cards_n = 0;

    for (int i = 0; i < CITY_COUNT; i++) {
        if (!(wc_chosen & (1u << i))) continue;
        char day_line[48], clock[12];
        const char *half;
        world_texts(i, now, day_line, sizeof(day_line), clock, sizeof(clock), &half);
        lv_obj_t *card = ui_box(wc_list, 330, 96);
        lv_obj_set_style_radius(card, 30, 0);
        lv_obj_set_style_bg_color(card, C_SURF, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, world_toggle, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);

        lv_obj_t *name = ui_label(card, CITIES[i].name, F24, C_TEXT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, 16);
        lv_obj_t *day = ui_label(card, day_line, F16, C_DIM);
        lv_obj_align(day, LV_ALIGN_BOTTOM_LEFT, 22, -16);

        lv_obj_t *time = ui_label(card, clock, F36, c);
        lv_obj_align(time, LV_ALIGN_RIGHT_MID, half[0] ? -58 : -22, 0);
        lv_obj_t *ampm = NULL;
        if (half[0]) {
            ampm = ui_label(card, half, F16, c);
            lv_obj_align(ampm, LV_ALIGN_RIGHT_MID, -22, 8);
        }
        wc_cards[wc_cards_n++] = (wc_card_t){.city = i, .day = day, .time = time, .half = ampm};
    }

    lv_obj_t *head = ui_label(wc_list, wc_chosen ? "ADD A CITY" : "PICK A CITY", F16, C_DIM);
    lv_obj_set_style_text_letter_space(head, 2, 0);
    lv_obj_set_style_margin_top(head, 10, 0);
    for (int i = 0; i < CITY_COUNT; i++) {
        if (wc_chosen & (1u << i)) continue;
        lv_obj_t *row = ui_pill_btn(wc_list, CITIES[i].name, C_SURF, C_TEXT, world_toggle, (void *)(intptr_t)i);
        lv_obj_set_width(row, 300);
    }
    ui_list_refresh(wc_list);
    lv_obj_scroll_to_y(wc_list, keep, LV_ANIM_OFF);
}

static void world_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_SETTINGS) world_build();
    else if (e == EVT_MINUTE) world_tick();
}

static void world_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    wc_list = NULL;
    wc_cards_n = 0;
}

static void world_create(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, world_closed, LV_EVENT_DELETE, NULL);
    if (!wc_loaded) {
        wc_loaded = true;
        svc_kv_load("world", &wc_chosen, sizeof(wc_chosen));
    }
    wc_list = ui_list(scr);
    bus_watch(scr, world_event, NULL);
    world_build();
    lv_obj_scroll_to_y(wc_list, 0, LV_ANIM_OFF);
}

const app_t app_world = {
    .id = "world", .name = "World Clock", .icon = &ic_globe_48, .color = WORLD_COLOR, .create = world_create,
};

/* --------------------------------------------------------------- calendar
 * A month at a time, with a dot under each day that has something on. Tap a
 * day to see what. The entries come from the Orbit phone app: the next month
 * of them is already on the watch, and any other day or month is asked for
 * when you look at it. */

#define MONTH_COLOR 0xF28B82
#define DAY_WAIT_MS 5000

static lv_obj_t *cal_scr, *cal_grid, *cal_title, *cal_page;
static int cal_year, cal_month, cal_day; /* month 1..12; day 0 while the month is showing */
static int cal_asked_month;              /* the month last asked of the phone, as year * 12 + month */
static bool cal_waiting;                 /* the phone has been asked about the open day */
static uint32_t cal_asked;

/* Does an entry fall on this date (days since 1970, by the watch's clock)? */
static bool touches(const agenda_item_t *it, long date)
{
    time_t shift = it->all_day ? 0 : (time_t)CFG.tz_minutes * 60; /* all-day entries are dates already */
    time_t last = it->end > it->start ? it->end - 1 : it->start;
    return date >= (long)((it->start + shift) / 86400) && date <= (long)((last + shift) / 86400);
}

/* The button at the foot of a day. It sits inside the page it closes, so the
 * page is removed once this event is over. */
static void calendar_to_month(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!cal_day) return;
    cal_day = 0;
    lv_obj_delete_async(cal_page);
    cal_page = NULL;
}

static void day_build(void)
{
    static const char *const days[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const months[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (cal_page) lv_obj_delete(cal_page);
    cal_page = ui_box(cal_scr, SCR, SCR);
    lv_obj_set_style_bg_color(cal_page, C_BG, 0);
    lv_obj_set_style_bg_opa(cal_page, LV_OPA_COVER, 0);
    lv_obj_add_flag(cal_page, LV_OBJ_FLAG_CLICKABLE); /* nothing reaches the month underneath */
    lv_obj_t *list = ui_list(cal_page);

    char buf[40];
    snprintf(buf, sizeof(buf), "%s %d %s", days[weekday(cal_year, cal_month, cal_day)], cal_day, months[cal_month - 1]);
    lv_obj_t *head = ui_label(list, buf, F20, lv_color_hex(MONTH_COLOR));
    lv_obj_set_style_text_letter_space(head, 2, 0);

    /* What the phone said about this day if it has answered; until then,
     * whatever the upcoming list already holds for it. */
    long date = days_from_civil(cal_year, cal_month, cal_day);
    time_t now = svc_time();
    bool answered = S.day.known && S.day.year == cal_year && S.day.month == cal_month && S.day.day == cal_day;
    int shown = 0;
    if (answered) {
        for (int i = 0; i < S.day.count; i++, shown++) agenda_card(list, &S.day.items[i], now, MONTH_COLOR);
    } else {
        for (int i = 0; i < S.agenda_count && shown < DAY_MAX; i++) {
            if (!touches(&S.agenda[i], date)) continue;
            agenda_card(list, &S.agenda[i], now, MONTH_COLOR);
            shown++;
        }
    }

    const char *note = NULL;
    if (!answered && cal_waiting) {
        note = "Checking with your phone";
    } else if (!shown) {
        /* Without an answer, an empty day is only known to be empty if the
         * upcoming list reaches it and was not cut short. */
        long today = (long)((now + (time_t)CFG.tz_minutes * 60) / 86400);
        bool covered = S.agenda_known && S.agenda_count < AGENDA_MAX && date >= today && date < today + AGENDA_DAYS;
        note = answered || covered ? "Nothing on this day" : "Connect the Orbit app on your phone to see this day";
    }
    if (note) {
        lv_obj_t *l = ui_label(list, note, F20, C_DIM);
        lv_obj_set_width(l, 290);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_margin_ver(l, shown ? 4 : 36, 0);
    }
    lv_obj_t *back = ui_pill_btn(list, LV_SYMBOL_LEFT "  Month", C_SURF2, C_TEXT, calendar_to_month, NULL);
    lv_obj_set_style_margin_top(back, 6, 0);
    ui_list_refresh(list);
}

static void calendar_pick(lv_event_t *e)
{
    cal_day = (int)(intptr_t)lv_event_get_user_data(e);
    S.day.year = (uint16_t)cal_year, S.day.month = (uint8_t)cal_month, S.day.day = (uint8_t)cal_day;
    S.day.known = false, S.day.count = 0;
    cal_waiting = svc_calendar_day(cal_year, cal_month, cal_day);
    cal_asked = svc_millis();
    day_build();
}

/* Back from a day to the month. */
static bool calendar_back(void)
{
    if (!cal_day) return false;
    cal_day = 0;
    lv_obj_delete(cal_page);
    cal_page = NULL;
    return true;
}

static void calendar_build(void)
{
    static const char *const months[12] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                           "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
    lv_color_t c = lv_color_hex(MONTH_COLOR);
    struct tm now;
    svc_localtime(&now);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", months[cal_month - 1], cal_year);
    lv_label_set_text(cal_title, buf);
    lv_obj_clean(cal_grid);

    /* Which days have something on: what the phone said about this month,
     * plus whatever the upcoming list shows of it. A month the watch does
     * not hold is asked for, once. */
    uint32_t busy = 0;
    bool held = false;
    for (int i = 0; i < BUSY_MONTHS; i++) {
        if (S.busy[i].year == cal_year && S.busy[i].month == cal_month) busy = S.busy[i].days, held = true;
    }
    if (!held && cal_asked_month != cal_year * 12 + cal_month && svc_calendar_month(cal_year, cal_month)) {
        cal_asked_month = cal_year * 12 + cal_month;
    }
    int days = month_days(cal_year, cal_month);
    long first = days_from_civil(cal_year, cal_month, 1);
    for (int i = 0; i < S.agenda_count; i++) {
        for (int d = 1; d <= days; d++) {
            if (touches(&S.agenda[i], first + d - 1)) busy |= 1u << d;
        }
    }

    int lead = (weekday(cal_year, cal_month, 1) + 6) % 7; /* weeks start on Monday */
    bool this_month = now.tm_year + 1900 == cal_year && now.tm_mon + 1 == cal_month;
    for (int d = 1; d <= days; d++) {
        int cell = lead + d - 1, col = cell % 7, row = cell / 7;
        bool today = this_month && d == now.tm_mday;
        lv_obj_t *box = ui_box(cal_grid, 36, 36);
        lv_obj_set_pos(box, col * 44 + 4, row * 37);
        lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(box, today ? c : C_SURF2, 0);
        lv_obj_set_style_bg_opa(box, today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(box, 3);
        lv_obj_add_event_cb(box, calendar_pick, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)d);
        snprintf(buf, sizeof(buf), "%d", d);
        lv_obj_center(ui_label(box, buf, F20, today ? lv_color_black() : (col >= 5 ? C_DIM : C_TEXT)));
        if (busy & (1u << d)) {
            lv_obj_t *dot = ui_box(box, 5, 5);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, today ? lv_color_black() : c, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -1);
        }
    }
}

static void calendar_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (d == 0) {
        struct tm now;
        svc_localtime(&now);
        cal_year = now.tm_year + 1900, cal_month = now.tm_mon + 1;
    } else {
        cal_month += d;
        if (cal_month < 1) cal_month = 12, cal_year--;
        if (cal_month > 12) cal_month = 1, cal_year++;
    }
    calendar_build();
}

static void calendar_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    struct tm now;
    svc_localtime(&now);
    /* Rebuild when the phone sends something, and at midnight. */
    if (e == EVT_AGENDA || (e == EVT_MINUTE && now.tm_hour == 0 && now.tm_min == 0)) {
        calendar_build();
        if (cal_day) day_build();
    }
}

/* Stops waiting for a phone that does not answer about the open day. */
static void calendar_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!cal_day || !cal_waiting) return;
    if (!S.day.known && svc_millis() - cal_asked < DAY_WAIT_MS) return;
    cal_waiting = false;
    if (!S.day.known) day_build();
}

static void calendar_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    cal_scr = cal_page = NULL;
    cal_day = 0;
}

static void calendar_create(lv_obj_t *scr)
{
    struct tm now;
    svc_localtime(&now);
    cal_year = now.tm_year + 1900, cal_month = now.tm_mon + 1;
    cal_scr = scr, cal_page = NULL, cal_day = 0, cal_asked_month = 0, cal_waiting = false;
    lv_obj_add_event_cb(scr, calendar_closed, LV_EVENT_DELETE, NULL);

    cal_title = ui_label(scr, "", F20, lv_color_hex(MONTH_COLOR));
    lv_obj_set_style_text_letter_space(cal_title, 2, 0);
    lv_obj_align(cal_title, LV_ALIGN_TOP_MID, 0, 54);

    static const char *const heads[7] = {"M", "T", "W", "T", "F", "S", "S"};
    for (int i = 0; i < 7; i++) {
        lv_obj_align(ui_label(scr, heads[i], F14, C_FAINT), LV_ALIGN_TOP_MID, (i - 3) * 44, 90);
    }
    cal_grid = ui_box(scr, 308, 222);
    lv_obj_remove_flag(cal_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(cal_grid, LV_ALIGN_TOP_MID, 0, 114);

    static const struct {
        const char *text;
        int step, x;
    } keys[3] = {{LV_SYMBOL_LEFT, -1, -92}, {"Today", 0, 0}, {LV_SYMBOL_RIGHT, 1, 92}};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = ui_pill_btn(scr, keys[i].text, C_SURF2, C_TEXT, calendar_step, (void *)(intptr_t)keys[i].step);
        lv_obj_set_height(b, 46);
        if (keys[i].step) lv_obj_set_style_pad_hor(b, 22, 0);
        lv_obj_align(b, LV_ALIGN_TOP_MID, keys[i].x, 346);
    }
    bus_watch(scr, calendar_event, NULL);
    ui_timer(scr, calendar_tick, 500, NULL);
    calendar_build();
}

const app_t app_month = {
    .id = "calendar", .name = "Calendar", .icon = &ic_month_48, .color = MONTH_COLOR,
    .create = calendar_create, .back = calendar_back,
};

/* ------------------------------------------------------------------- moon */

#define MOON_COLOR 0xE8E6D8
#define MOON_R 96
#define SYNODIC 29.530588853

static lv_obj_t *mo_disc, *mo_name, *mo_lit, *mo_next;
static double mo_shift; /* days added to the clock; only the simulator sets it */
static double mo_place_lat; /* latitude of the saved place, read when the app opens */

/* Days since the last new moon. */
static double moon_age(void)
{
    double days = ((double)svc_time() - 947182440.0) / 86400.0 + mo_shift; /* new moon of 6 January 2000 */
    double age = fmod(days, SYNODIC);
    return age < 0 ? age + SYNODIC : age;
}

static void moon_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    double phase = moon_age() / SYNODIC;
    /* South of the equator the moon is seen the other way up. */
    bool flip = (S.gps.fix ? S.gps.lat : mo_place_lat) < 0;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE, d.bg_color = lv_color_hex(0x1D1D1D);
    lv_area_t whole = {cx - MOON_R, cy - MOON_R, cx + MOON_R, cy + MOON_R};
    lv_draw_rect(layer, &d, &whole);

    /* The lit part, a row at a time: from the edge to where the shadow falls. */
    d.radius = 0, d.bg_color = lv_color_hex(MOON_COLOR);
    float edge = (float)cos(phase * 6.283185307);
    for (int y = -MOON_R + 1; y < MOON_R; y++) {
        float w = sqrtf((float)(MOON_R * MOON_R - y * y));
        float from = phase < 0.5 ? w * edge : -w, to = phase < 0.5 ? w : -w * edge;
        if (flip) {
            float swap = -from;
            from = -to, to = swap;
        }
        int x0 = cx + (int)lroundf(from), x1 = cx + (int)lroundf(to) - 1;
        if (x1 < x0) continue;
        lv_area_t line = {x0, cy + y, x1, cy + y};
        lv_draw_rect(layer, &d, &line);
    }
}

static void moon_paint(void)
{
    double age = moon_age(), phase = age / SYNODIC, quarter = SYNODIC / 4;
    int lit = (int)lround((1.0 - cos(phase * 6.283185307)) * 50.0);
    /* The four named moments last a day or two; the rest is in between. */
    const char *name;
    if (age < 1.0 || age > SYNODIC - 1.0) name = "New moon";
    else if (fabs(age - quarter) < 0.75) name = "First quarter";
    else if (fabs(age - 2 * quarter) < 1.0) name = "Full moon";
    else if (fabs(age - 3 * quarter) < 0.75) name = "Last quarter";
    else if (age < quarter) name = "Waxing crescent";
    else if (age < 2 * quarter) name = "Waxing gibbous";
    else if (age < 3 * quarter) name = "Waning gibbous";
    else name = "Waning crescent";
    lv_label_set_text(mo_name, name);

    char buf[40];
    snprintf(buf, sizeof(buf), "%d%% lit", lit);
    lv_label_set_text(mo_lit, buf);
    double to_full = fmod(SYNODIC / 2 - age + SYNODIC, SYNODIC), to_new = SYNODIC - age;
    bool full_next = to_full < to_new;
    int days = (int)lround(full_next ? to_full : to_new);
    if (days <= 0) snprintf(buf, sizeof(buf), "%s moon today", full_next ? "Full" : "New");
    else snprintf(buf, sizeof(buf), "%s moon in %d day%s", full_next ? "Full" : "New", days, days == 1 ? "" : "s");
    lv_label_set_text(mo_next, buf);
    lv_obj_invalidate(mo_disc);
}

static void moon_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_MINUTE) moon_paint();
}

static void moon_create(lv_obj_t *scr)
{
    /* The saved place only picks which way up the moon is drawn; read it once, not per repaint. */
    double place[2] = {0, 0};
    svc_kv_load("place", place, sizeof(place));
    mo_place_lat = place[0];
    mo_disc = ui_box(scr, MOON_R * 2 + 2, MOON_R * 2 + 2);
    lv_obj_remove_flag(mo_disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(mo_disc, LV_ALIGN_CENTER, 0, -52);
    lv_obj_add_event_cb(mo_disc, moon_draw, LV_EVENT_DRAW_MAIN, NULL);
    mo_name = ui_label(scr, "", F24, C_TEXT);
    lv_obj_align(mo_name, LV_ALIGN_CENTER, 0, 76);
    mo_lit = ui_label(scr, "", F20, lv_color_hex(MOON_COLOR));
    lv_obj_align(mo_lit, LV_ALIGN_CENTER, 0, 110);
    mo_next = ui_label(scr, "", F16, C_DIM);
    lv_obj_align(mo_next, LV_ALIGN_CENTER, 0, 142);
    bus_watch(scr, moon_event, NULL);
    moon_paint();
}

const app_t app_moon = {
    .id = "moon", .name = "Moon", .icon = &ic_moon_48, .color = MOON_COLOR, .create = moon_create,
};

#ifdef ORBIT_SIM
void extra_sim_moon(double days)
{
    mo_shift = days;
    moon_paint();
}
#endif
