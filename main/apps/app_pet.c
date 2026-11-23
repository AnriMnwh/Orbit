/* Pet: a small creature that lives on the watch.
 *
 * It gets hungry, bored, tired and grubby in real time (also while the watch
 * is off), and walking with the watch cheers it up. Needs are stored x100 so
 * slow per-minute drift does not get lost to rounding.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define PET_COLOR 0x7CE0D6
#define PET_VERSION 1
#define FULL 10000
#define MAX_CATCHUP_MIN (72 * 60)

typedef struct {
    uint8_t version;
    uint8_t asleep;
    uint8_t hatched;
    uint8_t taps;      /* taps on the egg so far */
    uint16_t food, joy, energy, clean; /* 0..FULL */
    uint32_t born;     /* UTC seconds */
    uint32_t last;     /* last time the needs were advanced */
    uint32_t steps_seen;
} pet_t;

static pet_t P;
static bool loaded;

enum { MOOD_ASLEEP, MOOD_HAPPY, MOOD_OK, MOOD_HUNGRY, MOOD_BORED, MOOD_TIRED, MOOD_GRUBBY };

static uint16_t bump(uint16_t v, int delta)
{
    int n = (int)v + delta;
    return (uint16_t)(n < 0 ? 0 : (n > FULL ? FULL : n));
}

static void pet_save(void)
{
    svc_kv_save("pet", &P, sizeof(P));
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

    P.food = bump(P.food, -13 * minutes);   /* about 8 points an hour */
    P.clean = bump(P.clean, -7 * minutes);
    if (P.asleep) {
        P.energy = bump(P.energy, 23 * minutes);
        if (P.energy >= FULL) P.asleep = 0; /* wakes up by itself once rested */
    } else {
        P.energy = bump(P.energy, -8 * minutes);
        P.joy = bump(P.joy, -10 * minutes);
    }
}

static void pet_load(void)
{
    if (loaded) return;
    loaded = true;
    if (!svc_kv_load("pet", &P, sizeof(P)) || P.version != PET_VERSION) {
        P = (pet_t){.version = PET_VERSION, .food = 7000, .joy = 7000, .energy = 8000, .clean = 9000};
        P.steps_seen = S.steps;
    }
    pet_advance();
}

static int pet_mood(void)
{
    if (P.asleep) return MOOD_ASLEEP;
    uint16_t low = P.food;
    int mood = MOOD_HUNGRY;
    if (P.joy < low) low = P.joy, mood = MOOD_BORED;
    if (P.energy < low) low = P.energy, mood = MOOD_TIRED;
    if (P.clean < low) low = P.clean, mood = MOOD_GRUBBY;
    if (low < 3000) return mood;
    return (P.food + P.joy + P.energy + P.clean) / 4 > 6500 ? MOOD_HAPPY : MOOD_OK;
}

/* Age in days, used for how grown the pet looks. */
static int pet_days(void)
{
    if (!P.born || !svc_time_valid()) return 0;
    uint32_t now = (uint32_t)svc_time();
    return now > P.born ? (int)((now - P.born) / 86400) : 0;
}

/* ------------------------------------------------------------------- view */

static lv_obj_t *v_scr, *v_pet, *v_mood, *v_arc[4], *v_sleep;
static uint32_t v_phase;
static int v_hop;        /* frames left of a happy hop */
static int v_munch;      /* frames left of eating */

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
    int days = pet_days();
    int size = days >= 3 ? 150 : (days >= 1 ? 134 : 116); /* grows over its first days */
    lv_color_t body = lv_color_hex(PET_COLOR);
    lv_color_t dark = lv_color_hex(0x10262A);

    /* Breathing: slow while asleep, a bounce while hopping. */
    float breathe = sinf(v_phase * (mood == MOOD_ASLEEP ? 0.07f : 0.16f));
    int lift = v_hop > 0 ? (int)(34 * sinf((float)v_hop / 14.0f * 3.14159f)) : 0;
    int w = size + (int)(4 * breathe), h = size - 14 - (int)(4 * breathe);
    int by = cy + 16 - lift + (size - h) / 2;

    rect(layer, cx, cy + 16 + size / 2 + 2, size - lift, 20, lv_color_hex(0x15171C), LV_OPA_COVER);

    /* Ears first so the body overlaps them. */
    int ear = size / 4;
    rect(layer, cx - w / 2 + ear / 2 + 6, by - h / 2 + ear / 3, ear, ear, body, LV_OPA_COVER);
    rect(layer, cx + w / 2 - ear / 2 - 6, by - h / 2 + ear / 3, ear, ear, body, LV_OPA_COVER);
    rect(layer, cx, by, w, h, body, LV_OPA_COVER);
    rect(layer, cx, by + h / 5, w * 6 / 10, h / 2, lv_color_hex(0xB4F1EA), LV_OPA_COVER);
    if (P.clean < 3000) {
        rect(layer, cx - w / 4, by + h / 4, 16, 12, lv_color_hex(0x6E7F73), LV_OPA_COVER);
        rect(layer, cx + w / 5, by - h / 8, 12, 10, lv_color_hex(0x6E7F73), LV_OPA_COVER);
        rect(layer, cx + w / 3, by + h / 5, 10, 8, lv_color_hex(0x6E7F73), LV_OPA_COVER);
    }

    int ex = w / 5, ey = by - h / 10;
    bool blink = v_phase % 70 < 3;
    if (mood == MOOD_ASLEEP) {
        arc(layer, cx - ex, ey - 4, 10, 20, 160, 4, dark);
        arc(layer, cx + ex, ey - 4, 10, 20, 160, 4, dark);
    } else if (blink) {
        rect(layer, cx - ex, ey, 20, 5, dark, LV_OPA_COVER);
        rect(layer, cx + ex, ey, 20, 5, dark, LV_OPA_COVER);
    } else {
        int eh = mood == MOOD_TIRED ? 14 : 26;
        rect(layer, cx - ex, ey, 20, eh, dark, LV_OPA_COVER);
        rect(layer, cx + ex, ey, 20, eh, dark, LV_OPA_COVER);
        rect(layer, cx - ex + 4, ey - eh / 4, 7, 7, lv_color_white(), LV_OPA_COVER);
        rect(layer, cx + ex + 4, ey - eh / 4, 7, 7, lv_color_white(), LV_OPA_COVER);
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
}

static void float_done(lv_anim_t *a)
{
    lv_obj_delete(a->var);
}

static void float_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y(obj, v, 0);
}

/* A little icon that drifts up from the pet and disappears. */
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
    static const char *const moods[] = {"Fast asleep", "Happy", "Content", "Hungry", "Bored", "Sleepy", "Needs a wash"};
    if (!v_scr) return;
    lv_label_set_text(v_mood, P.hatched ? moods[pet_mood()] : "Tap the egg");
    const uint16_t vals[4] = {P.food, P.joy, P.energy, P.clean};
    for (int i = 0; i < 4; i++) ui_arc_set(v_arc[i], P.hatched ? vals[i] / 100 : 0);
    lv_obj_set_style_bg_color(v_sleep, P.asleep ? lv_color_hex(0xB69CFF) : C_SURF2, 0);
    ui_icon_set(ui_btn_icon(v_sleep), NULL, P.asleep ? lv_color_black() : C_TEXT);
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

static void pet_feed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("eat")) return;
    if (P.food > 9400) {
        shell_toast("Already full");
        return;
    }
    P.food = bump(P.food, 2500);
    P.clean = bump(P.clean, -300);
    v_munch = 30;
    float_icon(&ic_apple_28, lv_color_hex(0xFFB45A), 0);
    pet_save();
    pet_paint();
}

static void pet_play(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("play")) return;
    if (P.energy < 1200) {
        shell_toast("Too tired to play");
        return;
    }
    P.joy = bump(P.joy, 2200);
    P.energy = bump(P.energy, -900);
    P.food = bump(P.food, -400);
    v_hop = 14;
    float_icon(&ic_heart_28, lv_color_hex(0xFF6B9A), -30);
    float_icon(&ic_heart_28, lv_color_hex(0xFF6B9A), 34);
    pet_save();
    pet_paint();
}

static void pet_wash(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!pet_awake_for("wash")) return;
    P.clean = FULL;
    float_icon(&ic_drop_28, lv_color_hex(0x58B6FF), -36);
    float_icon(&ic_drop_28, lv_color_hex(0x58B6FF), 30);
    pet_save();
    pet_paint();
}

static void pet_sleep(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!P.hatched) return;
    P.asleep = !P.asleep;
    pet_save();
    pet_paint();
}

static void pet_tapped(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!P.hatched) {
        if (++P.taps >= 3) {
            P.hatched = 1;
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
    /* Walking is play: every 400 steps lifts its spirits a little. */
    if (S.steps < P.steps_seen) P.steps_seen = S.steps;
    while (S.steps - P.steps_seen >= 400) {
        P.steps_seen += 400;
        if (P.hatched) P.joy = bump(P.joy, 500);
    }
    pet_save();
    pet_paint();
}

static void pet_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    v_scr = NULL;
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
    v_phase = 1, v_hop = 0, v_munch = 0;

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
    lv_obj_add_event_cb(v_pet, pet_tapped, LV_EVENT_CLICKED, NULL);

    lv_obj_t *feed = ui_round_btn(scr, 68, C_SURF2, &ic_apple_28, C_TEXT, pet_feed, NULL);
    place_on_ring(feed, 172, 144.0f);
    lv_obj_t *play = ui_round_btn(scr, 68, C_SURF2, &ic_ball_28, C_TEXT, pet_play, NULL);
    place_on_ring(play, 172, 108.0f);
    lv_obj_t *wash = ui_round_btn(scr, 68, C_SURF2, &ic_drop_28, C_TEXT, pet_wash, NULL);
    place_on_ring(wash, 172, 72.0f);
    v_sleep = ui_round_btn(scr, 68, C_SURF2, &ic_zzz_28, C_TEXT, pet_sleep, NULL);
    place_on_ring(v_sleep, 172, 36.0f);

    lv_obj_add_event_cb(scr, pet_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, pet_event, NULL);
    ui_timer(scr, pet_frame, 60, NULL);
    pet_paint();
}

/* Looks after the pet without the app being open (the assistant uses this).
 * Returns a short sentence describing what happened. */
const char *pet_command(const char *action)
{
    static const char *const moods[] = {"asleep", "happy", "content", "hungry", "bored", "sleepy", "in need of a wash"};
    static char out[64];
    pet_load();
    if (strcmp(action, "reset") == 0) {
        P = (pet_t){.version = PET_VERSION, .food = 7000, .joy = 7000, .energy = 8000, .clean = 9000};
        P.steps_seen = S.steps;
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
    else if (strcmp(action, "wash") == 0) P.clean = FULL;
    pet_save();
    pet_paint();
    snprintf(out, sizeof(out), "Done. The pet is now %s.", moods[pet_mood()]);
    return out;
}

const app_t app_pet = {
    .id = "pet", .name = "Pet", .icon = &ic_paw_48, .color = PET_COLOR, .create = pet_create,
};

#ifdef ORBIT_SIM
/* Lets the simulator show a grown, hatched pet without waiting days. */
void pet_sim_preset(int mood)
{
    pet_load();
    P.hatched = 1;
    P.born = (uint32_t)svc_time() - 4 * 86400;
    P.last = (uint32_t)svc_time();
    P.asleep = 0;
    P.food = 8200, P.joy = 9000, P.energy = 7400, P.clean = 8800;
    if (mood == 1) P.food = 1500, P.clean = 2400, P.joy = 4200;
    if (mood == 2) P.asleep = 1, P.energy = 3000;
}
#endif
