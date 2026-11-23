/* Look and feel: palette, type scale and the handful of building blocks every
 * screen is made from. The panel is AMOLED, so the canvas is true black and
 * colour is spent sparingly: one accent, plus a signature hue per app. */
#pragma once

#include "../assets/gen/fonts.h"
#include "../assets/gen/icons.h"
#include "../core/orbit.h"
#include "../core/svc.h"

#define C_BG lv_color_hex(0x000000)
/* Neutral dark greys, as on Wear OS. These exact values stay neutral after
 * the panel's 16-bit colour rounding; nearby ones pick up a green tint. */
#define C_SURF lv_color_hex(0x212121)
#define C_SURF2 lv_color_hex(0x313131)
#define C_LINE lv_color_hex(0x424242)
#define C_TEXT lv_color_hex(0xF1F3F4)
#define C_DIM lv_color_hex(0xAEB3BB)
#define C_FAINT lv_color_hex(0x62676F)
#define C_GOOD lv_color_hex(0x4ADE80)
#define C_WARN lv_color_hex(0xFFC53D)
#define C_BAD lv_color_hex(0xFF5D5D)

#define ACCENT_COUNT 6
lv_color_t ui_accent(void);
lv_color_t ui_accent_at(int index);

#define F14 (&lv_font_montserrat_14)
#define F16 (&lv_font_montserrat_16)
#define F20 (&lv_font_montserrat_20)
#define F24 (&lv_font_montserrat_24)
#define F28 (&lv_font_montserrat_28)
#define F36 (&lv_font_montserrat_36)
#define F48 (&lv_font_montserrat_48)

/* A colour blended towards black, for tinted fills that sit behind an icon. */
lv_color_t ui_tint(lv_color_t c, int percent);

lv_obj_t *ui_screen(void);
/* A bare, non-scrolling, transparent container. */
lv_obj_t *ui_box(lv_obj_t *parent, int w, int h);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);
/* Single line, centred, truncated with dots to `width`. */
lv_obj_t *ui_label_fit(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color, int width);
lv_obj_t *ui_icon(lv_obj_t *parent, const lv_image_dsc_t *src, lv_color_t color);
void ui_icon_set(lv_obj_t *icon, const lv_image_dsc_t *src, lv_color_t color);
/* Small caption at the top of an app screen. */
lv_obj_t *ui_title(lv_obj_t *parent, const char *text);

/* Round button with an icon. `cb` gets `user` as event user data. */
lv_obj_t *ui_round_btn(lv_obj_t *parent, int size, lv_color_t bg, const lv_image_dsc_t *icon, lv_color_t fg,
                       lv_event_cb_t cb, void *user);
lv_obj_t *ui_btn_icon(lv_obj_t *btn); /* the icon inside a ui_round_btn */
lv_obj_t *ui_pill_btn(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void *user);

/* Display-only arc. Angles in degrees, 0 = 3 o'clock, clockwise. */
lv_obj_t *ui_arc(lv_obj_t *parent, int diameter, int width, lv_color_t track, lv_color_t color, int start, int sweep);
void ui_arc_set(lv_obj_t *arc, int percent);

/* Timer that dies with `owner`, so screens need no teardown code. */
lv_timer_t *ui_timer(lv_obj_t *owner, lv_timer_cb_t cb, uint32_t period_ms, void *user);

/* The current time, curved along the top of the screen. */
lv_obj_t *ui_time_text(lv_obj_t *screen);
/* A full-screen scrolling column whose rows shrink and fade towards the top
 * and bottom, with a curved scroll indicator on the right edge. Add rows to
 * the returned object, then call ui_list_refresh() once they are in. */
lv_obj_t *ui_list(lv_obj_t *parent);
void ui_list_refresh(lv_obj_t *list);

/* A full-screen picture painted by hand (ui/glow.c): fill the field, then
 * show it. The buffers die with the object. */
#define GLOW_N 118 /* field cells per side; cell (x, y) lands on pixel (4x, 4y) */
lv_obj_t *glow_create(lv_obj_t *parent);
uint8_t *glow_field(lv_obj_t *glow); /* GLOW_N * GLOW_N * 3 bytes of RGB; NULL if memory ran out */
void glow_show(lv_obj_t *glow);

/* ------------------------------------------------------------------- shell */

typedef struct app {
    const char *id;
    const char *name;
    const lv_image_dsc_t *icon;
    uint32_t color;
    void (*create)(lv_obj_t *screen);
    bool (*back)(void);  /* optional: return true if the app handled Back itself */
    bool keep_awake;     /* display must not rest while this app is open */
    bool own_gestures;   /* app uses swipes itself, so swipe-right does not go back */
    bool no_clock;       /* app uses the top edge itself, so no curved clock there */
} app_t;

extern const app_t *const APPS[];
extern const int APP_COUNT;
const app_t *app_find(const char *id);

enum { BTN_POWER, BTN_SIDE };
enum { PRESS_SHORT, PRESS_LONG, PRESS_DOUBLE };

void shell_init(void);
void shell_open(const app_t *app);
void shell_back(void);
void shell_home(void);
const app_t *shell_current(void);
const app_t *shell_recent(int n);   /* n-th most recently opened app, or NULL */
void shell_toast(const char *text);
void shell_wake(void);              /* any user activity */
void shell_raised(void);            /* wrist raise: wakes unless a quiet mode is on */
void shell_rest(void);              /* drop to always-on or dark now */
void shell_button(int button, int press);
void shell_show_tile(int col, int row);
void shell_rebuild_tiles(void);     /* after CFG.tiles changed */
void shell_notification(const notif_t *n); /* open one notification in full */

/* Something still running that earns a chip on the watch face. */
typedef struct {
    const app_t *app;
    const lv_image_dsc_t *icon;
    char text[28];
} ongoing_t;
bool shell_ongoing(ongoing_t *out);
bool timer_ongoing(char *text, size_t len, bool *stopwatch);

void tile_create(lv_obj_t *tile, int kind); /* builds one of the TILE_ kinds */
extern const app_t app_faces, app_welcome;
void shell_confirm(const char *question, const char *yes, lv_event_cb_t on_yes);
void shell_alert(const char *title, const char *text);

void agent_listen_on_open(void);    /* next time the assistant opens, start listening */
void timer_start(int seconds);
const char *pet_command(const char *action); /* feed, play, wash, sleep, status */

/* Home tiles and overlays (each lives in its own file). */
void watchface_create(lv_obj_t *tile);
void launcher_create(lv_obj_t *tile);
void quick_create(lv_obj_t *tile);
void notifs_create(lv_obj_t *tile);
void weather_tile_create(lv_obj_t *tile);
void agenda_tile_create(lv_obj_t *tile);
void alarm_get(char *text, size_t len, bool *on);
void alarm_set_on(bool on);
void duties_init(void); /* alarm and step history, running in the background */
void zzz_init(void);    /* the ZZZ app's charge alert, running in the background */
/* Watch faces that live with their apps. `home` marks the live face, which
 * stops moving while an app covers it. */
void vision_face(lv_obj_t *root, bool ambient, bool home);
void zzz_face(lv_obj_t *root, bool ambient, bool home);
/* What those two apps hold, for the phone link. Setting emits EVT_GAME. */
void vision_get(int *element, int *region);
void vision_set(int element, int region);           /* below zero: leave that one alone */
void zzz_get(int *charge, int64_t *full, int *done); /* full: UTC second it reaches 240, 0 if not counting */
void zzz_set(int charge, int spend, int done);      /* charge or done below zero: leave it */
/* One calendar entry as a card, added to a ui_list. */
void agenda_card(lv_obj_t *list, const agenda_item_t *it, time_t now, uint32_t color);
extern const app_t app_launcher;
lv_obj_t *aod_create(void);
void aod_tick(lv_obj_t *aod);
