/* Music tools: Synth (pads, also a Bluetooth MIDI controller), Metronome and
 * Tuner. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/synth.h"
#include "../ui/ui.h"

/* ------------------------------------------------------------------ synth */

#define SYNTH_COLOR 0xD0BCFF
#define PADS 8

static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const SCALE_NAMES[] = {"Major", "Minor", "Pentatonic", "Blues"};
static const uint8_t SCALES[][PADS] = {
    {0, 2, 4, 5, 7, 9, 11, 12},
    {0, 2, 3, 5, 7, 8, 10, 12},
    {0, 3, 5, 7, 10, 12, 15, 17},
    {0, 3, 5, 6, 7, 10, 12, 15},
};
#define SCALE_COUNT 4

/* What the player last chose; kept while the watch is on. */
static struct {
    int preset, scale, key, octave;
    bool tilt;
} Y = {.octave = 4, .tilt = true};

static lv_obj_t *sy_scr, *sy_pad[PADS], *sy_panel;
static int sy_sounding[PADS]; /* note each pad is holding, or -1 */
static bool sy_midi_here;     /* this app switched MIDI mode on */
static int sy_last_cc = -1;

static int pad_note(int pad)
{
    return 12 * (Y.octave + 1) + Y.key + SCALES[Y.scale][pad];
}

static void pads_label(void)
{
    for (int i = 0; i < PADS; i++) {
        lv_label_set_text(lv_obj_get_child(sy_pad[i], 0), NOTE_NAMES[pad_note(i) % 12]);
    }
}

static void pad_event(lv_event_t *e)
{
    int pad = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        int note = pad_note(pad);
        sy_sounding[pad] = note;
        synth_note_on(note, 100);
        svc_midi(0x90, (uint8_t)note, 100);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && sy_sounding[pad] >= 0) {
        synth_note_off(sy_sounding[pad]);
        svc_midi(0x80, (uint8_t)sy_sounding[pad], 0);
        sy_sounding[pad] = -1;
    }
}

/* Tilting the watch opens and closes the filter, and sends it as MIDI
 * controller 74 (brightness) when a MIDI host is listening. */
static void synth_tilt(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!Y.tilt) return;
    float x, y, z;
    svc_tilt(&x, &y, &z);
    float amount = 0.5f - y * 0.9f;
    amount = amount < 0 ? 0 : (amount > 1 ? 1 : amount);
    synth_cutoff(amount);
    int cc = (int)(amount * 127);
    if (S.link.midi_mode && LV_ABS(cc - sy_last_cc) >= 2) {
        svc_midi(0xB0, 74, (uint8_t)cc);
        sy_last_cc = cc;
    }
}

/* ---- the sound panel ---- */

static void panel_build(void);

static void panel_close(lv_event_t *e)
{
    LV_UNUSED(e);
    if (sy_panel) lv_obj_delete(sy_panel);
    sy_panel = NULL;
}

static void panel_tap(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: Y.preset = (Y.preset + 1) % SYNTH_PRESETS, synth_preset(Y.preset); break;
    case 1: Y.scale = (Y.scale + 1) % SCALE_COUNT; break;
    case 2: Y.key = (Y.key + 1) % 12; break;
    case 3: Y.octave = Y.octave >= 6 ? 2 : Y.octave + 1; break;
    case 4:
        Y.tilt = !Y.tilt;
        if (!Y.tilt) synth_cutoff(0.5f);
        break;
    case 5:
        sy_midi_here = !S.link.midi_mode;
        svc_link_midi_mode(!S.link.midi_mode);
        break;
    }
    synth_all_off();
    pads_label();
    panel_build();
}

static void panel_row(lv_obj_t *list, const char *label, const char *value, int id)
{
    lv_obj_t *r = ui_box(list, 320, 64);
    lv_obj_set_style_radius(r, 32, 0);
    lv_obj_set_style_bg_color(r, C_SURF, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, C_SURF2, LV_STATE_PRESSED);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, panel_tap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)id);
    lv_obj_align(ui_label(r, label, F20, C_TEXT), LV_ALIGN_LEFT_MID, 26, 0);
    lv_obj_align(ui_label(r, value, F20, lv_color_hex(SYNTH_COLOR)), LV_ALIGN_RIGHT_MID, -26, 0);
}

static void panel_build(void)
{
    char buf[16];
    int scroll = sy_panel ? lv_obj_get_scroll_y(lv_obj_get_child(sy_panel, 0)) : 0;
    panel_close(NULL);
    sy_panel = ui_box(sy_scr, SCR, SCR);
    lv_obj_set_style_bg_color(sy_panel, C_BG, 0);
    lv_obj_set_style_bg_opa(sy_panel, LV_OPA_COVER, 0);
    lv_obj_t *list = ui_list(sy_panel);
    lv_obj_set_style_pad_top(list, 70, 0);

    panel_row(list, "Sound", SYNTH_PRESET_NAMES[Y.preset], 0);
    panel_row(list, "Scale", SCALE_NAMES[Y.scale], 1);
    panel_row(list, "Key", NOTE_NAMES[Y.key], 2);
    snprintf(buf, sizeof(buf), "%d", Y.octave);
    panel_row(list, "Octave", buf, 3);
    panel_row(list, "Tilt filter", Y.tilt ? "On" : "Off", 4);
    panel_row(list, "Bluetooth MIDI", S.link.midi_mode ? "On" : "Off", 5);
    if (S.link.midi_mode) {
        const char *state = S.link.status == LINK_CONNECTED ? "Connected" : "Pair \"Orbit MIDI\" in your music app";
        lv_obj_t *hint = ui_label(list, state, F16, C_DIM);
        lv_obj_set_width(hint, 280);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *done = ui_pill_btn(list, "Done", lv_color_hex(SYNTH_COLOR), lv_color_black(), panel_close, NULL);
    lv_obj_set_style_margin_top(done, 6, 0);
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);
    ui_list_refresh(list);
}

static void panel_open(lv_event_t *e)
{
    LV_UNUSED(e);
    panel_build();
}

static bool synth_back(void)
{
    if (!sy_panel) return false;
    panel_close(NULL);
    return true;
}

static void synth_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_LINK && sy_panel) panel_build();
}

static void synth_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    synth_all_off();
    svc_synth(false);
    if (sy_midi_here && S.link.midi_mode) svc_link_midi_mode(false);
    sy_midi_here = false;
    sy_scr = NULL, sy_panel = NULL;
}

static void synth_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(SYNTH_COLOR);
    sy_scr = scr, sy_panel = NULL;
    /* The root note in the middle, the rest of the scale around it. */
    for (int i = 0; i < PADS; i++) {
        bool centre = i == 0;
        sy_sounding[i] = -1;
        sy_pad[i] = ui_round_btn(scr, centre ? 124 : 112, centre ? c : ui_tint(c, 26), NULL, C_TEXT, NULL, NULL);
        lv_obj_set_ext_click_area(sy_pad[i], 0);
        lv_obj_set_style_bg_color(sy_pad[i], lv_color_white(), LV_STATE_PRESSED);
        if (!centre) {
            float a = (-90.0f + (i - 1) * 360.0f / 7.0f) * 3.14159265f / 180.0f;
            lv_obj_align(sy_pad[i], LV_ALIGN_CENTER, (int)(156 * cosf(a)), (int)(156 * sinf(a)));
        } else {
            lv_obj_center(sy_pad[i]);
        }
        lv_obj_center(ui_label(sy_pad[i], "", F24, centre ? lv_color_black() : c));
        lv_obj_add_event_cb(sy_pad[i], pad_event, LV_EVENT_ALL, (void *)(intptr_t)i);
    }
    pads_label();

    /* The gap between the two lowest pads holds the way into the sound panel. */
    lv_obj_t *gear = ui_round_btn(scr, 50, C_SURF2, &ic_gear_28, C_TEXT, panel_open, NULL);
    lv_obj_align(gear, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_add_event_cb(scr, synth_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, synth_event, NULL);
    ui_timer(scr, synth_tilt, 50, NULL);
    synth_preset(Y.preset);
    svc_synth(true);
}

const app_t app_synth = {
    .id = "synth", .name = "Synth", .icon = &ic_piano_48, .color = SYNTH_COLOR, .create = synth_create,
    .back = synth_back, .keep_awake = true, .own_gestures = true, .no_clock = true,
};

/* -------------------------------------------------------------- metronome */

#define METRO_COLOR 0xFFB68C

static int me_bpm = 100, me_bar = 4;
static bool me_running;
static lv_obj_t *me_num, *me_go, *me_dots, *me_bar_label;
static uint32_t me_tap_ms;

static void metro_apply(void)
{
    synth_metronome(me_running ? me_bpm : 0, me_bar);
}

static void metro_paint(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", me_bpm);
    lv_label_set_text(me_num, buf);
    snprintf(buf, sizeof(buf), "%d / 4", me_bar);
    lv_label_set_text(me_bar_label, buf);
    ui_icon_set(ui_btn_icon(me_go), me_running ? &ic_stop_48 : &ic_play_48, lv_color_black());
    lv_obj_clean(me_dots);
    for (int i = 0; i < me_bar; i++) {
        lv_obj_t *d = ui_box(me_dots, 16, 16);
        lv_obj_set_style_radius(d, 8, 0);
        lv_obj_set_style_bg_color(d, C_SURF2, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
}

static void metro_beat(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!me_running) return;
    /* The engine counts beats as it plays them, so the lights match the sound. */
    int now = (int)((synth_beat() + (uint32_t)me_bar - 1) % (uint32_t)me_bar);
    lv_color_t c = lv_color_hex(METRO_COLOR);
    for (int i = 0; i < me_bar && i < (int)lv_obj_get_child_count(me_dots); i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(me_dots, i), i == now ? (i == 0 ? C_TEXT : c) : C_SURF2, 0);
    }
}

static void metro_step(lv_event_t *e)
{
    me_bpm += (int)(intptr_t)lv_event_get_user_data(e);
    me_bpm = me_bpm < 40 ? 40 : (me_bpm > 220 ? 220 : me_bpm);
    metro_apply();
    metro_paint();
}

static void metro_go(lv_event_t *e)
{
    LV_UNUSED(e);
    me_running = !me_running;
    metro_apply();
    metro_paint();
}

/* Tap the number in time to set the tempo. */
static void metro_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t now = svc_millis(), gap = now - me_tap_ms;
    me_tap_ms = now;
    if (gap < 250 || gap > 1500) return;
    me_bpm = (int)(60000 / gap);
    metro_apply();
    metro_paint();
}

static void metro_bar(lv_event_t *e)
{
    LV_UNUSED(e);
    me_bar = me_bar >= 6 ? 2 : me_bar + 1;
    metro_apply();
    metro_paint();
}

static void metro_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    me_running = false;
    synth_metronome(0, me_bar);
    svc_synth(false);
}

static void metro_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(METRO_COLOR);
    me_running = false;
    me_dots = ui_box(scr, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(me_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(me_dots, 12, 0);
    lv_obj_align(me_dots, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t *tap = ui_box(scr, 220, 120);
    lv_obj_add_event_cb(tap, metro_tap, LV_EVENT_PRESSED, NULL);
    lv_obj_align(tap, LV_ALIGN_CENTER, 0, -48);
    me_num = ui_label(tap, "", &font_num_big, C_TEXT);
    lv_obj_align(me_num, LV_ALIGN_CENTER, 0, -8);
    lv_obj_align(ui_label(tap, "BPM  " LV_SYMBOL_BULLET "  tap to set", F14, C_FAINT), LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *down = ui_round_btn(scr, 68, C_SURF2, &ic_minus_28, C_TEXT, metro_step, (void *)(intptr_t)-2);
    lv_obj_align(down, LV_ALIGN_CENTER, -150, -48);
    lv_obj_t *up = ui_round_btn(scr, 68, C_SURF2, &ic_plus_28, C_TEXT, metro_step, (void *)(intptr_t)2);
    lv_obj_align(up, LV_ALIGN_CENTER, 150, -48);

    me_go = ui_round_btn(scr, 96, c, &ic_play_48, lv_color_black(), metro_go, NULL);
    lv_obj_align(me_go, LV_ALIGN_CENTER, 0, 84);
    lv_obj_t *bar = ui_pill_btn(scr, "", C_SURF, C_DIM, metro_bar, NULL);
    lv_obj_set_height(bar, 44);
    me_bar_label = lv_obj_get_child(bar, 0);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 166);

    lv_obj_add_event_cb(scr, metro_closed, LV_EVENT_DELETE, NULL);
    ui_timer(scr, metro_beat, 30, NULL);
    svc_synth(true);
    metro_paint();
}

const app_t app_metronome = {
    .id = "metronome", .name = "Metronome", .icon = &ic_metronome_48, .color = METRO_COLOR,
    .create = metro_create, .keep_awake = true,
};

/* ------------------------------------------------------------------ tuner */

#define TUNER_COLOR 0xA8DAB5

static lv_obj_t *tu_note, *tu_cents, *tu_needle, *tu_hz;

static void tuner_paint(void)
{
    if (!S.tuner.valid || S.tuner.hz < 30) {
        lv_label_set_text(tu_note, "--");
        lv_label_set_text(tu_cents, "Play a note");
        lv_label_set_text(tu_hz, "");
        lv_obj_add_flag(tu_needle, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* Semitones from A4 (440 Hz), then how far off the nearest note we are. */
    float semis = 12.0f * log2f(S.tuner.hz / 440.0f);
    int nearest = (int)lroundf(semis);
    int cents = (int)lroundf((semis - nearest) * 100.0f);
    int midi = 69 + nearest;
    bool good = LV_ABS(cents) <= 5;
    char buf[24];

    snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[((midi % 12) + 12) % 12], midi / 12 - 1);
    lv_label_set_text(tu_note, buf);
    lv_obj_set_style_text_color(tu_note, good ? lv_color_hex(TUNER_COLOR) : C_TEXT, 0);
    if (good) snprintf(buf, sizeof(buf), "In tune");
    else snprintf(buf, sizeof(buf), "%d cents %s", LV_ABS(cents), cents < 0 ? "flat" : "sharp");
    lv_label_set_text(tu_cents, buf);
    snprintf(buf, sizeof(buf), "%.1f Hz", (double)S.tuner.hz);
    lv_label_set_text(tu_hz, buf);

    /* The needle rides the top arc: centre is in tune, +-50 cents at the ends. */
    float a = (-90.0f + cents * 0.9f) * 3.14159265f / 180.0f;
    lv_obj_remove_flag(tu_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(tu_needle, good ? lv_color_hex(TUNER_COLOR) : C_TEXT, 0);
    lv_obj_align(tu_needle, LV_ALIGN_CENTER, (int)(196 * cosf(a)), (int)(196 * sinf(a)));
}

static void tuner_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_TUNER) tuner_paint();
}

static void tuner_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_tuner(false);
}

static void tuner_create(lv_obj_t *scr)
{
    lv_obj_t *track = ui_arc(scr, 392, 6, C_SURF2, C_SURF2, 225, 90);
    lv_obj_center(track);
    lv_obj_t *mark = ui_box(scr, 6, 26);
    lv_obj_set_style_bg_color(mark, lv_color_hex(TUNER_COLOR), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mark, 3, 0);
    lv_obj_align(mark, LV_ALIGN_CENTER, 0, -196);
    tu_needle = ui_box(scr, 26, 26);
    lv_obj_set_style_radius(tu_needle, 13, 0);
    lv_obj_set_style_bg_opa(tu_needle, LV_OPA_COVER, 0);

    tu_note = ui_label(scr, "", F48, C_TEXT);
    lv_obj_align(tu_note, LV_ALIGN_CENTER, 0, -30);
    tu_cents = ui_label(scr, "", F24, C_DIM);
    lv_obj_align(tu_cents, LV_ALIGN_CENTER, 0, 52);
    tu_hz = ui_label(scr, "", F16, C_FAINT);
    lv_obj_align(tu_hz, LV_ALIGN_CENTER, 0, 90);

    lv_obj_add_event_cb(scr, tuner_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, tuner_event, NULL);
    svc_tuner(true);
    tuner_paint();
}

const app_t app_tuner = {
    .id = "tuner", .name = "Tuner", .icon = &ic_tuner_48, .color = TUNER_COLOR, .create = tuner_create,
    .keep_awake = true,
};
