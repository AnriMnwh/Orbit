/* Pet: a full-featured Tamagotchi companion on the watch.
 *
 * Evolves through Egg -> Baby -> Child -> Teen -> Adult with 4 distinct forms
 * based on care mistakes and discipline. Features Meal/Snack feeding, a
 * 3-round Look Left/Right mini-game, poops & cleaning, sickness & medicine,
 * attention calls, and a long-press stats sheet.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define PET_COLOR 0x7CE0D6
#define PET_VERSION 2
#define FULL 10000
#define MAX_CATCHUP_MIN (72 * 60)

enum { STAGE_EGG, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT };
enum { FORM_CLASSIC, FORM_ANGEL, FORM_STAR, FORM_GREMLIN };
enum { MOOD_ASLEEP, MOOD_HAPPY, MOOD_OK, MOOD_HUNGRY, MOOD_BORED, MOOD_TIRED, MOOD_GRUBBY, MOOD_SICK };

typedef struct {
    uint8_t version;
    uint8_t asleep;
    uint8_t hatched;
    uint8_t taps;
    uint8_t stage;
    uint8_t form;
    uint8_t poops;
    uint8_t sick;
    uint8_t attention;
    uint8_t discipline;
    uint8_t weight;
    uint8_t snacks_row;
    uint16_t food, joy, energy, clean;
    uint16_t care_mistakes;
    uint16_t wins;
    uint16_t poop_mins;
    uint32_t born;
    uint32_t last;
    uint32_t steps_seen;
    uint32_t total_steps;
} pet_t;

static pet_t P;
static bool loaded, alerted_sick;

static uint16_t bump(uint16_t v, int delta)
{
    int n = (int)v + delta;
    return (uint16_t)(n < 0 ? 0 : (n > FULL ? FULL : n));
}

static void pet_save(void)
{
    svc_kv_save("pet", &P, sizeof(P));
}

static int pet_days(void)
{
    if (!P.born || !svc_time_valid()) return 0;
    uint32_t now = (uint32_t)svc_time();
    return now > P.born ? (int)((now - P.born) / 86400) : 0;
}

static void pet_evolve(void)
{
    if (!P.hatched) {
        P.stage = STAGE_EGG;
        return;
    }
    int d = pet_days();
    uint8_t target = d >= 7 ? STAGE_ADULT : (d >= 3 ? STAGE_TEEN : (d >= 1 ? STAGE_CHILD : STAGE_BABY));
    if (target > P.stage) {
        P.stage = target;
        if (target >= STAGE_CHILD) {
            if (P.care_mistakes <= 1 && P.discipline >= 60) P.form = FORM_ANGEL;
            else if (P.care_mistakes <= 3 && P.wins >= 2) P.form = FORM_STAR;
            else if (P.care_mistakes >= 5) P.form = FORM_GREMLIN;
            else P.form = FORM_CLASSIC;
        }
    }
}

static void pet_advance(void)
{
    uint32_t now = (uint32_t)svc_time();
    if (!svc_time_valid() || P.last == 0 || now < P.last) {
        P.last = now;
        return;
    }
    int minutes = (int)((now - P.last) / 60);
    if (minutes <= 0) return;
    P.last += (uint32_t)minutes * 60;
    if (minutes > MAX_CATCHUP_MIN) minutes = MAX_CATCHUP_MIN;
    if (!P.hatched) return;

    P.food = bump(P.food, -12 * minutes);
    P.clean = bump(P.clean, -(6 + P.poops * 4) * minutes);
    if (P.asleep) {
        P.energy = bump(P.energy, 24 * minutes);
        if (P.energy >= FULL) P.asleep = 0;
    } else {
        P.energy = bump(P.energy, -8 * minutes);
        P.joy = bump(P.joy, -9 * minutes);
        P.poop_mins += (uint16_t)minutes;
        while (P.poop_mins >= 240 && P.poops < 3) {
            P.poop_mins -= 240;
            P.poops++;
        }
        if (P.poop_mins >= 240) P.poop_mins = 0;
    }
    if ((P.poops >= 2 || P.clean < 1500 || P.food < 1000) && !P.sick) {
        P.sick = 1;
        if (P.care_mistakes < 999) P.care_mistakes++;
    }
    if (!P.asleep && !P.sick && P.food > 4000 && P.joy < 4500 && !P.attention && (minutes % 37 == 0)) {
        P.attention = 1;
    }
    pet_evolve();
}

static void pet_load(void)
{
    if (loaded) return;
    loaded = true;
    if (!svc_kv_load("pet", &P, sizeof(P)) || P.version != PET_VERSION) {
        P = (pet_t){.version = PET_VERSION, .food = 7500, .joy = 7500, .energy = 8500, .clean = 9000,
                    .discipline = 40, .weight = 15};
        P.steps_seen = S.steps;
    }
    pet_advance();
}

static int pet_mood(void)
{
    if (P.sick) return MOOD_SICK;
    if (P.asleep) return MOOD_ASLEEP;
    uint16_t low = P.food;
    int mood = MOOD_HUNGRY;
    if (P.joy < low) low = P.joy, mood = MOOD_BORED;
    if (P.energy < low) low = P.energy, mood = MOOD_TIRED;
    if (P.clean < low) low = P.clean, mood = MOOD_GRUBBY;
    if (low < 3000) return mood;
    return (P.food + P.joy + P.energy + P.clean) / 4 > 6500 ? MOOD_HAPPY : MOOD_OK;
}

void pet_summary(int *health_pct, int *mood_out)
{
    pet_load();
    pet_advance();
    if (health_pct) {
        if (!P.hatched) *health_pct = 100;
        else {
            int avg = (int)((P.food + P.joy + P.energy + P.clean) / 400);
            if (P.sick) avg = avg > 30 ? avg - 30 : 5;
            *health_pct = avg;
        }
    }
    if (mood_out) *mood_out = pet_mood();
}

/* ------------------------------------------------------------------- view */

static lv_obj_t *v_scr, *v_pet, *v_mood, *v_arc[4], *v_ctx, *v_modal;
static uint32_t v_phase;
static int v_hop, v_munch;
static int v_game_round, v_game_wins, v_look_dir; /* mini-game state: 0=none, 1=left, 2=right */

static void rect(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = c;
    d.bg_opa = opa;
    lv_area_t a = {x - w / 2, y - h / 2, x + w / 2, y + h / 2};
    lv_draw_rect(layer, &d, &a);
}

static void arc(lv_layer_t *layer, int x, int y, int r, int start, int end, int width, lv_color_t c)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = x, d.center.y = y;
    d.radius = (uint16_t)r;
    d.start_angle = (lv_value_precise_t)start, d.end_angle = (lv_value_precise_t)end;
    d.width = width;
    d.color = c;
    d.rounded = 1;
    lv_draw_arc(layer, &d);
}

static void draw_egg(lv_layer_t *layer, int cx, int cy)
{
    int wobble = P.taps ? (int)(6 * sinf(v_phase * 0.9f)) : 0;
    rect(layer, cx, cy + 86, 150, 22, lv_color_hex(0x15171C), LV_OPA_COVER);
    rect(layer, cx + wobble, cy + 6, 132, 164, lv_color_hex(0xF2EEE3), LV_OPA_COVER);
    rect(layer, cx + wobble - 26, cy - 24, 26, 26, lv_color_hex(PET_COLOR), LV_OPA_COVER);
    rect(layer, cx + wobble + 22, cy + 22, 34, 34, lv_color_hex(PET_COLOR), LV_OPA_COVER);
    rect(layer, cx + wobble + 10, cy - 46, 16, 16, lv_color_hex(PET_COLOR), LV_OPA_COVER);
}

static void pet_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;

    if (!P.hatched) {
        draw_egg(layer, cx, cy);
        return;
    }

    int mood = pet_mood();
    int size = P.stage >= STAGE_ADULT ? 152 : (P.stage == STAGE_TEEN ? 138 : (P.stage == STAGE_CHILD ? 124 : 104));
    lv_color_t body = P.form == FORM_ANGEL ? lv_color_hex(0xB8E8FF)
                    : (P.form == FORM_STAR ? lv_color_hex(0xFFE082)
                    : (P.form == FORM_GREMLIN ? lv_color_hex(0xD0BCFF) : lv_color_hex(PET_COLOR)));
    lv_color_t dark = lv_color_hex(0x10262A);

    float breathe = sinf(v_phase * (mood == MOOD_ASLEEP ? 0.07f : 0.16f));
    int lift = v_hop > 0 ? (int)(34 * sinf((float)v_hop / 14.0f * 3.14159f)) : 0;
    int w = size + (int)(4 * breathe), h = size - 14 - (int)(4 * breathe);
    int by = cy + 16 - lift + (size - h) / 2;

    rect(layer, cx, cy + 16 + size / 2 + 2, size - lift, 20, lv_color_hex(0x15171C), LV_OPA_COVER);

    /* Poops on the floor. */
    for (int i = 0; i < P.poops; i++) {
        int px = cx + (i == 0 ? -82 : (i == 1 ? 82 : -56));
        int py = cy + 68;
        rect(layer, px, py + 6, 26, 14, lv_color_hex(0x6E4E37), LV_OPA_COVER);
        rect(layer, px, py - 2, 18, 12, lv_color_hex(0x855E42), LV_OPA_COVER);
        rect(layer, px, py - 9, 10, 8, lv_color_hex(0x9C7152), LV_OPA_COVER);
    }

    /* Form accessories + ears. */
    int ear = size / 4;
    if (P.form == FORM_GREMLIN) {
        rect(layer, cx - w / 3, by - h / 2 - 4, 16, 22, lv_color_hex(0xFF8A80), LV_OPA_COVER);
        rect(layer, cx + w / 3, by - h / 2 - 4, 16, 22, lv_color_hex(0xFF8A80), LV_OPA_COVER);
    } else {
        rect(layer, cx - w / 2 + ear / 2 + 6, by - h / 2 + ear / 3, ear, ear, body, LV_OPA_COVER);
        rect(layer, cx + w / 2 - ear / 2 - 6, by - h / 2 + ear / 3, ear, ear, body, LV_OPA_COVER);
    }
    if (P.form == FORM_ANGEL) arc(layer, cx, by - h / 2 - 14, 24, 0, 360, 4, lv_color_hex(0xFDD663));
    if (P.form == FORM_STAR) {
        rect(layer, cx, by - h / 2 - 10, 6, 18, C_DIM, LV_OPA_COVER);
        rect(layer, cx, by - h / 2 - 20, 16, 16, lv_color_hex(0xFDD663), LV_OPA_COVER);
    }

    rect(layer, cx, by, w, h, body, LV_OPA_COVER);
    rect(layer, cx, by + h / 5, w * 6 / 10, h / 2, lv_color_hex(0xE0FBF7), 180);
    if (P.clean < 3000) {
        rect(layer, cx - w / 4, by + h / 4, 16, 12, lv_color_hex(0x6E7F73), LV_OPA_COVER);
        rect(layer, cx + w / 5, by - h / 8, 12, 10, lv_color_hex(0x6E7F73), LV_OPA_COVER);
    }

    int look_dx = v_look_dir == 1 ? -10 : (v_look_dir == 2 ? 10 : 0);
    int ex = w / 5, ey = by - h / 10;
    bool blink = v_phase % 70 < 3;
    if (mood == MOOD_ASLEEP) {
        arc(layer, cx - ex, ey - 4, 10, 20, 160, 4, dark);
        arc(layer, cx + ex, ey - 4, 10, 20, 160, 4, dark);
    } else if (mood == MOOD_SICK) {
        rect(layer, cx - ex, ey, 18, 5, dark, LV_OPA_COVER);
        rect(layer, cx + ex, ey, 18, 5, dark, LV_OPA_COVER);
        rect(layer, cx + w / 2 - 4, by - h / 4, 14, 14, C_BAD, LV_OPA_COVER);
    } else if (blink) {
        rect(layer, cx - ex, ey, 20, 5, dark, LV_OPA_COVER);
        rect(layer, cx + ex, ey, 20, 5, dark, LV_OPA_COVER);
    } else {
        int eh = mood == MOOD_TIRED ? 14 : 26;
        rect(layer, cx - ex + look_dx, ey, 20, eh, dark, LV_OPA_COVER);
        rect(layer, cx + ex + look_dx, ey, 20, eh, dark, LV_OPA_COVER);
        rect(layer, cx - ex + 4 + look_dx, ey - eh / 4, 7, 7, lv_color_white(), LV_OPA_COVER);
        rect(layer, cx + ex + 4 + look_dx, ey - eh / 4, 7, 7, lv_color_white(), LV_OPA_COVER);
    }

    int my = by + h / 6;
    if (v_munch > 0) rect(layer, cx, my + 4, 18, (v_munch / 3) % 2 ? 16 : 8, dark, LV_OPA_COVER);
    else if (mood == MOOD_HAPPY) arc(layer, cx, my - 4, 14, 25, 155, 5, dark);
    else if (mood == MOOD_ASLEEP || mood == MOOD_OK) rect(layer, cx, my + 4, 16, 5, dark, LV_OPA_COVER);
    else arc(layer, cx, my + 16, 13, 205, 335, 5, dark);

    if (mood == MOOD_HAPPY || v_hop > 0) {
        rect(layer, cx - ex - 20, my - 2, 20, 12, lv_color_hex(0xFF8FB0), 200);
        rect(layer, cx + ex + 20, my - 2, 20, 12, lv_color_hex(0xFF8FB0), 200);
    }
    if (P.attention) {
        rect(layer, cx + w / 2 + 12, by - h / 2, 22, 22, C_WARN, LV_OPA_COVER);
    }
}

static void float_done(lv_anim_t *a)
{
    lv_obj_delete(a->var);
}

static void float_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(obj, v, 0);
}

static void float_icon(const lv_image_dsc_t *icon, lv_color_t c, int x)
{
    if (!v_scr) return;
    lv_obj_t *i = ui_icon(v_scr, icon, c);
    lv_obj_align(i, LV_ALIGN_CENTER, x, -40);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, i);
    lv_anim_set_values(&a, 0, -90);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_exec_cb(&a, float_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, float_done);
    lv_anim_start(&a);
}

static void pet_paint(void)
{
    static const char *const moods[] = {"Fast asleep", "Happy", "Content", "Hungry", "Bored", "Sleepy", "Needs a wash", "Sick!"};
    if (!v_scr) return;
    lv_label_set_text(v_mood, P.hatched ? moods[pet_mood()] : "Tap the egg");
    const uint16_t vals[4] = {P.food, P.joy, P.energy, P.clean};
    for (int i = 0; i < 4; i++) ui_arc_set(v_arc[i], P.hatched ? vals[i] / 100 : 0);

    if (P.sick) {
        lv_obj_set_style_bg_color(v_ctx, C_BAD, 0);
        ui_icon_set(ui_btn_icon(v_ctx), &ic_heart_28, lv_color_black());
    } else if (P.attention) {
        lv_obj_set_style_bg_color(v_ctx, C_WARN, 0);
        ui_icon_set(ui_btn_icon(v_ctx), &ic_bell_28, lv_color_black());
    } else {
        lv_obj_set_style_bg_color(v_ctx, P.asleep ? lv_color_hex(0xB69CFF) : C_SURF2, 0);
        ui_icon_set(ui_btn_icon(v_ctx), &ic_zzz_28, P.asleep ? lv_color_black() : C_TEXT);
    }
    lv_obj_invalidate(v_pet);
}

static bool pet_awake_for(const char *what)
{
    if (!P.hatched) {
        shell_toast("It hasn't hatched yet");
        return false;
    }
    if (P.asleep) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Too sleepy to %s", what);
        shell_toast(buf);
        return false;
    }
    return true;
}

static void modal_close(lv_event_t *e)
{
    LV_UNUSED(e);
    if (v_modal) lv_obj_delete(v_modal);
    v_modal = NULL;
    v_look_dir = 0;
}

static lv_obj_t *modal_open(const char *title)
{
    modal_close(NULL);
    v_modal = ui_box(v_scr, SCR, SCR);
    lv_obj_set_style_bg_color(v_modal, C_BG, 0);
    lv_obj_set_style_bg_opa(v_modal, LV_OPA_90, 0);
    lv_obj_add_flag(v_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui_label(v_modal, title, F20, ui_accent()), LV_ALIGN_TOP_MID, 0, 56);
    return v_modal;
}

static void feed_choice(lv_event_t *e)
{
    bool snack = lv_event_get_user_data(e) != NULL;
    modal_close(NULL);
    if (P.food > 9400 && !snack) {
        shell_toast("Already full");
        return;
    }
    if (snack) {
        P.food = bump(P.food, 1000);
        P.joy = bump(P.joy, 1800);
        if (P.weight < 99) P.weight += 2;
        if (++P.snacks_row >= 3) {
            P.sick = 1;
            shell_toast("Too many snacks!");
        }
    } else {
        P.food = bump(P.food, 2500);
        P.clean = bump(P.clean, -300);
        P.snacks_row = 0;
        if (P.weight < 99) P.weight += 1;
    }
    v_munch = 30;
    float_icon(&ic_apple_28, lv_color_hex(0xFFB45A), 0);
    pet_save();
    pet_paint();
}

static void pet_feed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("eat")) return;
    lv_obj_t *m = modal_open("FEED");
    lv_obj_t *meal = ui_pill_btn(m, "Meal  (+Food)", ui_accent(), lv_color_black(), feed_choice, NULL);
    lv_obj_align(meal, LV_ALIGN_CENTER, 0, -32);
    lv_obj_t *snack = ui_pill_btn(m, "Snack  (+Joy)", C_SURF2, C_TEXT, feed_choice, (void *)1);
    lv_obj_align(snack, LV_ALIGN_CENTER, 0, 44);
    lv_obj_t *x = ui_pill_btn(m, "Cancel", C_SURF, C_DIM, modal_close, NULL);
    lv_obj_align(x, LV_ALIGN_BOTTOM_MID, 0, -44);
}

static void game_guess(lv_event_t *e)
{
    int guess = (int)(intptr_t)lv_event_get_user_data(e);
    int actual = (int)lv_rand(1, 2);
    v_look_dir = actual;
    if (guess == actual) {
        v_game_wins++;
        v_hop = 14;
        if (CFG.sound) svc_sound(SND_OK);
    } else if (CFG.sound) {
        svc_sound(SND_TAP);
    }
    v_game_round++;
    if (v_game_round >= 3) {
        modal_close(NULL);
        P.joy = bump(P.joy, 1800 + v_game_wins * 700);
        P.energy = bump(P.energy, -800);
        P.food = bump(P.food, -400);
        if (P.weight > 10) P.weight--;
        if (v_game_wins >= 2) P.wins++;
        char msg[32];
        snprintf(msg, sizeof(msg), "Won %d of 3 rounds!", v_game_wins);
        shell_toast(msg);
        float_icon(&ic_heart_28, lv_color_hex(0xFF6B9A), 0);
        pet_save();
        pet_paint();
    } else {
        lv_obj_t *lbl = lv_obj_get_child(v_modal, 1);
        char buf[32];
        snprintf(buf, sizeof(buf), "Round %d/3  (Score %d)", v_game_round + 1, v_game_wins);
        lv_label_set_text(lbl, buf);
    }
}

static void pet_play(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("play")) return;
    if (P.energy < 1200) {
        shell_toast("Too tired to play");
        return;
    }
    v_game_round = 0;
    v_game_wins = 0;
    v_look_dir = 0;
    lv_obj_t *m = modal_open("LOOK LEFT OR RIGHT");
    lv_obj_set_style_bg_opa(m, LV_OPA_70, 0);
    lv_obj_t *sub = ui_label(m, "Round 1/3  (Score 0)", F16, C_DIM);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *l = ui_round_btn(m, 88, C_SURF2, &ic_prev_48, C_TEXT, game_guess, (void *)1);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, -76, -64);
    lv_obj_t *r = ui_round_btn(m, 88, C_SURF2, &ic_next_48, C_TEXT, game_guess, (void *)2);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 76, -64);
}

static void pet_wash(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("wash")) return;
    P.clean = FULL;
    P.poops = 0;
    P.poop_mins = 0;
    float_icon(&ic_drop_28, lv_color_hex(0x58B6FF), -36);
    float_icon(&ic_drop_28, lv_color_hex(0x58B6FF), 30);
    pet_save();
    pet_paint();
}

static void pet_context(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!P.hatched) return;
    if (P.sick) {
        P.sick = 0;
        P.snacks_row = 0;
        alerted_sick = false;
        float_icon(&ic_heart_28, C_GOOD, 0);
        shell_toast("All better!");
    } else if (P.attention) {
        P.attention = 0;
        if (P.discipline < 100) P.discipline = (uint8_t)LV_MIN(P.discipline + 15, 100);
        P.joy = bump(P.joy, 500);
        shell_toast("Discipline +15");
    } else {
        P.asleep = !P.asleep;
    }
    pet_save();
    pet_paint();
}

static void pet_stats(lv_event_t *e)
{
    LV_UNUSED(e);
    static const char *const stages[] = {"Egg", "Baby", "Child", "Teen", "Adult"};
    static const char *const forms[] = {"Classic", "Angel", "Star", "Gremlin"};
    lv_obj_t *m = modal_open("PET STATS");
    char buf[192];
    snprintf(buf, sizeof(buf),
             "Stage: %s (%s)\nAge: %d days\nWeight: %u g\nDiscipline: %u%%\nGame wins: %u\nSteps together: %u",
             stages[P.stage % 5], forms[P.form % 4], pet_days(), P.weight, P.discipline, P.wins,
             (unsigned)P.total_steps);
    lv_obj_t *txt = ui_label(m, buf, F20, C_TEXT);
    lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(txt, LV_ALIGN_CENTER, 0, -10);
    lv_obj_t *ok = ui_pill_btn(m, "Close", C_SURF2, C_TEXT, modal_close, NULL);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static void pet_tapped(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!P.hatched) {
        if (++P.taps >= 3) {
            P.hatched = 1;
            P.stage = STAGE_BABY;
            P.born = svc_time_valid() ? (uint32_t)svc_time() : 0;
            P.last = (uint32_t)svc_time();
            v_hop = 14;
            shell_toast("It hatched!");
        }
    } else if (P.asleep) {
        return;
    } else {
        P.joy = bump(P.joy, 300);
        v_hop = 14;
        float_icon(&ic_heart_28, lv_color_hex(0xFF6B9A), 0);
    }
    pet_save();
    pet_paint();
}

static void pet_frame(lv_timer_t *t)
{
    LV_UNUSED(t);
    v_phase++;
    if (v_hop > 0) v_hop--;
    if (v_munch > 0) v_munch--;
    lv_obj_invalidate(v_pet);
}

static void pet_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e != EVT_MINUTE) return;
    pet_advance();
    if (S.steps < P.steps_seen) P.steps_seen = S.steps;
    uint32_t delta = S.steps - P.steps_seen;
    if (delta > 0) {
        P.total_steps += delta;
        P.steps_seen = S.steps;
        if (P.hatched) P.joy = bump(P.joy, (int)(delta / 2));
    }
    if (P.hatched && (P.sick || P.food < 1200) && !alerted_sick && !CFG.dnd && !CFG.bedtime && !S.theatre) {
        alerted_sick = true;
        shell_alert("Pet needs care", P.sick ? "Your pet is sick and needs medicine." : "Your pet is starving!");
    }
    pet_save();
    pet_paint();
}

static void pet_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    v_scr = NULL;
    v_modal = NULL;
    v_look_dir = 0;
    pet_save();
}

static void place_on_ring(lv_obj_t *o, int radius, float deg)
{
    float r = deg * 3.14159265f / 180.0f;
    lv_obj_align(o, LV_ALIGN_CENTER, (int)(radius * cosf(r)), (int)(radius * sinf(r)));
}

static void pet_create(lv_obj_t *scr)
{
    pet_load();
    pet_advance();
    v_scr = scr;
    v_modal = NULL;
    v_phase = 1, v_hop = 0, v_munch = 0, v_look_dir = 0;

    static const lv_image_dsc_t *const icons[4] = {&ic_apple_28, &ic_heart_28, &ic_bolt_28, &ic_drop_28};
    static const uint32_t colors[4] = {0xFFB45A, 0xFF6B9A, 0xFFE066, 0x58B6FF};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *box = ui_box(scr, 60, 60);
        place_on_ring(box, 168, 216.0f + i * 36.0f);
        v_arc[i] = ui_arc(box, 60, 5, C_SURF2, lv_color_hex(colors[i]), 270, 360);
        lv_obj_center(v_arc[i]);
        lv_obj_center(ui_icon(box, icons[i], lv_color_hex(colors[i])));
    }

    v_mood = ui_label(scr, "", F16, C_DIM);
    lv_obj_set_style_text_letter_space(v_mood, 1, 0);
    lv_obj_align(v_mood, LV_ALIGN_CENTER, 0, -112);

    v_pet = ui_box(scr, 240, 220);
    lv_obj_align(v_pet, LV_ALIGN_CENTER, 0, -6);
    lv_obj_add_flag(v_pet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v_pet, pet_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(v_pet, pet_tapped, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(v_pet, pet_stats, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *feed = ui_round_btn(scr, 68, C_SURF2, &ic_apple_28, C_TEXT, pet_feed, NULL);
    place_on_ring(feed, 172, 144.0f);
    lv_obj_t *play = ui_round_btn(scr, 68, C_SURF2, &ic_ball_28, C_TEXT, pet_play, NULL);
    place_on_ring(play, 172, 108.0f);
    lv_obj_t *wash = ui_round_btn(scr, 68, C_SURF2, &ic_drop_28, C_TEXT, pet_wash, NULL);
    place_on_ring(wash, 172, 72.0f);
    v_ctx = ui_round_btn(scr, 68, C_SURF2, &ic_zzz_28, C_TEXT, pet_context, NULL);
    place_on_ring(v_ctx, 172, 36.0f);

    lv_obj_add_event_cb(scr, pet_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, pet_event, NULL);
    ui_timer(scr, pet_frame, 60, NULL);
    pet_paint();
}

const char *pet_command(const char *action)
{
    static const char *const moods[] = {"asleep", "happy", "content", "hungry", "bored", "sleepy", "in need of a wash", "sick"};
    static char out[64];
    pet_load();
    if (strcmp(action, "reset") == 0) {
        P = (pet_t){.version = PET_VERSION, .food = 7500, .joy = 7500, .energy = 8500, .clean = 9000,
                    .discipline = 40, .weight = 15};
        P.steps_seen = S.steps;
        alerted_sick = false;
        pet_save();
        pet_paint();
        return "A new egg is waiting.";
    }
    pet_advance();
    if (!P.hatched) return "The pet has not hatched yet. Open the Pet app and tap the egg.";
    if (strcmp(action, "sleep") == 0) P.asleep = !P.asleep;
    else if (P.asleep && strcmp(action, "status") != 0) return "The pet is asleep.";
    else if (strcmp(action, "feed") == 0) P.food = bump(P.food, 2500);
    else if (strcmp(action, "play") == 0) P.joy = bump(P.joy, 2200), P.energy = bump(P.energy, -900);
    else if (strcmp(action, "wash") == 0) P.clean = FULL, P.poops = 0;
    pet_save();
    pet_paint();
    snprintf(out, sizeof(out), "Done. The pet is now %s.", moods[pet_mood()]);
    return out;
}

const app_t app_pet = {
    .id = "pet", .name = "Pet", .icon = &ic_paw_48, .color = PET_COLOR, .create = pet_create,
};

#ifdef ORBIT_SIM
void pet_sim_preset(int mood)
{
    pet_load();
    P.hatched = 1;
    P.stage = STAGE_ADULT;
    P.form = FORM_ANGEL;
    P.born = (uint32_t)svc_time() - 8 * 86400;
    P.last = (uint32_t)svc_time();
    P.asleep = 0;
    P.sick = 0;
    P.poops = 0;
    P.food = 8200, P.joy = 9000, P.energy = 7400, P.clean = 8800;
    if (mood == 1) P.food = 1500, P.clean = 2400, P.joy = 4200, P.poops = 1;
    if (mood == 2) P.asleep = 1, P.energy = 3000;
}
#endif
