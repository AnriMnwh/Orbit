/* Agenda: what is coming up in your calendar, as sent by the Orbit phone app,
 * plus the tile that shows the next thing. The watch has no calendar of its
 * own; it shows what the phone last sent (up to a month ahead) and drops
 * entries as they finish. The Calendar app shows the same entries by date. */
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define AGENDA_COLOR 0x8AB4F8
#define AGENDA_SHOWN 20 /* cards in the list; later ones are reached through Calendar */

/* Wall-clock fields for a UTC time. All-day entries are dated in UTC, which
 * is how phones store them, so they are not shifted. */
static struct tm local_of(time_t utc, bool all_day)
{
    time_t t = utc + (all_day ? 0 : (time_t)CFG.tz_minutes * 60);
    struct tm out = {0};
    struct tm *p = gmtime(&t);
    if (p) out = *p;
    return out;
}

static long day_of(time_t utc, bool all_day)
{
    return (long)((utc + (all_day ? 0 : (time_t)CFG.tz_minutes * 60)) / 86400);
}

static bool is_over(const agenda_item_t *it, time_t now)
{
    /* An all-day entry runs by the calendar where the watch is, not by UTC:
     * it ends when the local date reaches its end date, whatever the hour
     * that UTC midnight falls on here. */
    if (it->all_day) return day_of(now, false) >= day_of(it->end > it->start ? it->end : it->start + 86400, true);
    /* An entry with no length stays up for a quarter of an hour. */
    return (it->end > it->start ? it->end : it->start + 900) <= now;
}

static bool is_now(const agenda_item_t *it, time_t now)
{
    if (it->all_day) return day_of(it->start, true) <= day_of(now, false) && !is_over(it, now);
    return it->start <= now && !is_over(it, now);
}

static void day_text(char *buf, size_t len, const agenda_item_t *it)
{
    static const char *const days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    long today = day_of(svc_time(), false), day = day_of(it->start, it->all_day);
    struct tm t = local_of(it->start, it->all_day);
    if (day <= today) snprintf(buf, len, "Today");
    else if (day == today + 1) snprintf(buf, len, "Tomorrow");
    else snprintf(buf, len, "%s %d %s", days[t.tm_wday % 7], t.tm_mday, months[t.tm_mon % 12]);
}

static void clock_text(char *buf, size_t len, time_t utc)
{
    struct tm t = local_of(utc, false);
    char clock[12];
    fmt_clock(clock, sizeof(clock), &t, false);
    const char *half = fmt_ampm(&t);
    snprintf(buf, len, "%s%s%s", clock, half[0] ? " " : "", half);
}

static void time_text(char *buf, size_t len, const agenda_item_t *it, time_t now)
{
    char from[16], to[16];
    clock_text(from, sizeof(from), it->start);
    clock_text(to, sizeof(to), it->end);
    if (it->all_day) snprintf(buf, len, "All day");
    else if (it->end <= it->start) snprintf(buf, len, "%s", from);
    else if (is_now(it, now)) snprintf(buf, len, "Now  " LV_SYMBOL_BULLET "  until %s", to);
    else snprintf(buf, len, "%s - %s", from, to);
}

/* Changes whenever the list would look different: an entry ended or began. */
static int signature(void)
{
    time_t now = svc_time();
    int sig = 0;
    for (int i = 0; i < S.agenda_count; i++) {
        if (!is_over(&S.agenda[i], now)) sig += 16;
        if (is_now(&S.agenda[i], now)) sig += 1;
    }
    return sig;
}

/* ------------------------------------------------------------------- app */

static lv_obj_t *ag_list;
static int ag_sig;

/* One entry as a card: when, what, where. The Calendar app uses it too. */
void agenda_card(lv_obj_t *list, const agenda_item_t *it, time_t now, uint32_t color)
{
    char buf[48];
    lv_obj_t *card = ui_box(list, 330, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 30, 0);
    lv_obj_set_style_bg_color(card, C_SURF, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    time_text(buf, sizeof(buf), it, now);
    ui_label(card, buf, F16, is_now(it, now) ? C_GOOD : lv_color_hex(color));

    lv_obj_t *title = ui_label(card, it->title[0] ? it->title : "Busy", F24, C_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 290);
    lv_obj_set_style_max_height(title, 2 * lv_font_get_line_height(F24), 0);

    if (it->place[0]) {
        lv_obj_t *place = ui_label(card, it->place, F20, C_DIM);
        lv_label_set_long_mode(place, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(place, 290, lv_font_get_line_height(F20));
    }
}

static void agenda_more(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(app_find("calendar"));
}

static void agenda_build(void)
{
    lv_color_t c = lv_color_hex(AGENDA_COLOR);
    time_t now = svc_time();
    char last_day[24] = "", buf[48];
    int shown = 0, more = 0;
    lv_obj_clean(ag_list);
    ag_sig = signature();

    for (int i = 0; i < S.agenda_count; i++) {
        const agenda_item_t *it = &S.agenda[i];
        if (is_over(it, now)) continue;
        if (shown >= AGENDA_SHOWN) {
            more++;
            continue;
        }
        shown++;

        day_text(buf, sizeof(buf), it);
        if (strcmp(buf, last_day) != 0) {
            snprintf(last_day, sizeof(last_day), "%.23s", buf);
            for (char *p = buf; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
            lv_obj_t *head = ui_label(ag_list, buf, F16, C_DIM);
            lv_obj_set_style_text_letter_space(head, 2, 0);
            lv_obj_set_style_margin_top(head, shown > 1 ? 10 : 0, 0);
        }
        agenda_card(ag_list, it, now, AGENDA_COLOR);
    }
    if (more) {
        snprintf(buf, sizeof(buf), "%d more in Calendar", more);
        lv_obj_t *b = ui_pill_btn(ag_list, buf, C_SURF2, C_TEXT, agenda_more, NULL);
        lv_obj_set_style_margin_top(b, 10, 0);
    }

    if (!shown) {
        lv_obj_t *empty = ui_box(ag_list, 330, 300);
        lv_obj_align(ui_icon(empty, &ic_calendar_48, S.agenda_known ? c : C_FAINT), LV_ALIGN_CENTER, 0, -44);
        lv_obj_align(ui_label(empty, S.agenda_known ? "Nothing coming up" : "No calendar yet", F24, C_TEXT), LV_ALIGN_CENTER, 0, 14);
        lv_obj_t *why = ui_label(empty, S.agenda_known ? "Your calendar is clear" : "It comes from the Orbit app on an Android phone",
                                 F16, C_DIM);
        lv_obj_set_width(why, 290);
        lv_obj_set_style_text_align(why, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(why, LV_ALIGN_CENTER, 0, 64);
    }
    ui_list_refresh(ag_list);
}

static void agenda_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_AGENDA || e == EVT_SETTINGS || (e == EVT_MINUTE && signature() != ag_sig)) agenda_build();
}

static void agenda_create(lv_obj_t *scr)
{
    ag_list = ui_list(scr);
    bus_watch(scr, agenda_event, NULL);
    agenda_build();
}

const app_t app_agenda = {
    .id = "agenda", .name = "Agenda", .icon = &ic_calendar_48, .color = AGENDA_COLOR, .create = agenda_create,
};

/* ------------------------------------------------------------------ tile */

static lv_obj_t *nx_when, *nx_title, *nx_place, *nx_open;

static void tile_refresh(void)
{
    time_t now = svc_time();
    const agenda_item_t *next = NULL;
    long today = day_of(now, false);
    int after = 0; /* what else is on today */
    for (int i = 0; i < S.agenda_count; i++) {
        const agenda_item_t *it = &S.agenda[i];
        if (is_over(it, now)) continue;
        if (!next) next = it;
        else if (day_of(it->start, it->all_day) <= today) after++;
    }

    char buf[96];
    if (next) {
        char day[24], when[48];
        day_text(day, sizeof(day), next);
        time_text(when, sizeof(when), next, now);
        if (is_now(next, now) && !next->all_day) snprintf(buf, sizeof(buf), "%s", when);
        else snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_BULLET "  %s", day, when);
        lv_label_set_text(nx_when, buf);
        lv_obj_set_style_text_color(nx_when, is_now(next, now) ? C_GOOD : lv_color_hex(AGENDA_COLOR), 0);
        lv_label_set_text(nx_title, next->title[0] ? next->title : "Busy");
        lv_label_set_text(nx_place, next->place);
    } else {
        lv_label_set_text(nx_when, "");
        lv_label_set_text(nx_title, S.agenda_known ? "Nothing coming up" : "No calendar yet");
        lv_label_set_text(nx_place, S.agenda_known ? "Your calendar is clear" : "Needs the Orbit phone app");
    }
    if (after > 0) snprintf(buf, sizeof(buf), "%d more today", after);
    else snprintf(buf, sizeof(buf), "Open");
    lv_label_set_text(lv_obj_get_child(nx_open, 0), buf);
}

static void tile_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_AGENDA || e == EVT_MINUTE || e == EVT_SETTINGS || e == EVT_DISPLAY) tile_refresh();
}

static void tile_open(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_open(&app_agenda);
}

void agenda_tile_create(lv_obj_t *tile)
{
    lv_color_t c = lv_color_hex(AGENDA_COLOR);
    lv_obj_align(ui_icon(tile, &ic_calendar_28, c), LV_ALIGN_TOP_MID, 0, 44);
    nx_when = ui_label_fit(tile, "", F20, c, 340);
    lv_obj_align(nx_when, LV_ALIGN_CENTER, 0, -104);

    /* Up to two lines of title, centred on the same spot whichever it takes. */
    lv_obj_t *box = ui_box(tile, 340, 2 * lv_font_get_line_height(F28));
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -34);
    nx_title = ui_label(box, "", F28, C_TEXT);
    lv_label_set_long_mode(nx_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(nx_title, 340);
    lv_obj_set_style_max_height(nx_title, 2 * lv_font_get_line_height(F28), 0);
    lv_obj_set_style_text_align(nx_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(nx_title);

    nx_place = ui_label_fit(tile, "", F20, C_DIM, 300);
    lv_obj_align(nx_place, LV_ALIGN_CENTER, 0, 34);
    nx_open = ui_pill_btn(tile, "Open", C_SURF, C_DIM, tile_open, NULL);
    lv_obj_align(nx_open, LV_ALIGN_BOTTOM_MID, 0, -40);
    bus_watch(tile, tile_event, NULL);
    tile_refresh();
}
