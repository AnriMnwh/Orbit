/* Vision: the round screen as a glowing elemental gem, after the Visions of
 * Genshin Impact. Pick one of seven elements and one of seven regions; the
 * region decides the metal frame around the gem. It is also a watch face, so
 * it can be the always-on display.
 *
 * Everything here is drawn from scratch: the light inside the gem is painted
 * by hand (ui/glow.c), and the emblems and frames are simple strokes and
 * shapes of our own in the spirit of the game, not its artwork.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define VISION_COLOR 0x5CCFE6
#define RAD 0.01745329f
#define GEM_R 214   /* gem radius on the 0..255 scale where 255 is the screen edge (196 px) */
#define BAND 213    /* middle of the metal frame, in pixels from the centre */
#define ELEMENTS 7
#define REGIONS 7

typedef struct {
    const char *name;
    uint32_t deep, mid, light;
} element_t;

static const element_t ELEMENT[ELEMENTS] = {
    {"Anemo", 0x083F36, 0x35CFA6, 0xC8FFEB},   {"Geo", 0x4A2E00, 0xE5A416, 0xFFEFB0},
    {"Electro", 0x2A0E5E, 0x9F63F5, 0xEBD6FF}, {"Dendro", 0x1B4209, 0x86CC33, 0xE9FFBF},
    {"Hydro", 0x08235E, 0x2B8FF5, 0xCDEDFF},   {"Pyro", 0x560D04, 0xF5522A, 0xFFDDB0},
    {"Cryo", 0x0E4558, 0x5CCFE6, 0xEAFDFF},
};

typedef struct {
    const char *name;
    uint32_t metal, body, accent;
} region_t;

static const region_t REGION[REGIONS] = {
    {"Mondstadt", 0xDDE3EC, 0x565E6C, 0xE8C56A}, {"Liyue", 0xF0CC6E, 0x6B5217, 0xFFF0B8},
    {"Inazuma", 0xD9BC62, 0x4F3E1A, 0xA584F0},   {"Sumeru", 0xD8B863, 0x4E4219, 0x7CCB62},
    {"Fontaine", 0xCBD8EC, 0x46526A, 0x5F96EA},  {"Natlan", 0xD9935A, 0x57321A, 0xF0603C},
    {"Snezhnaya", 0xEAF2FA, 0x5A6878, 0xA6D2F5},
};

static struct {
    uint8_t element, region;
} V = {4, 4};
static bool v_loaded;

static void vision_load(void)
{
    if (v_loaded) return;
    v_loaded = true;
    svc_kv_load("vision", &V, sizeof(V));
    if (V.element >= ELEMENTS) V.element = 4;
    if (V.region >= REGIONS) V.region = 4;
}

/* ---------------------------------------------------------------- emblems */

/* Strokes on a -100..100 grid, y down. LN: from (a, b) to (c, d).
 * AR: centre (a, b), radius c, from d to e degrees (0 = 3 o'clock, clockwise). */
enum { LN, AR };
typedef struct {
    int8_t kind;
    int16_t a, b, c, d, e;
} seg_t;

typedef struct {
    const seg_t *spin; /* drawn `fold` times, turned evenly round the centre */
    uint8_t spin_n, fold;
    const seg_t *still;
    uint8_t still_n;
} emblem_t;

static const seg_t ANEMO_SPIN[] = {{AR, 0, -38, 38, 90, 310}};
static const seg_t GEO_STILL[] = {
    {LN, 0, -82, 60, 0, 0},  {LN, 60, 0, 0, 82, 0},  {LN, 0, 82, -60, 0, 0},  {LN, -60, 0, 0, -82, 0}, {LN, 0, -42, 30, 0, 0},
    {LN, 30, 0, 0, 42, 0},   {LN, 0, 42, -30, 0, 0}, {LN, -30, 0, 0, -42, 0}, {AR, 0, 0, 5, 0, 360},
};
static const seg_t ELECTRO_SPIN[] = {{AR, 0, 0, 74, -150, -70}, {LN, 25, -70, 16, -45, 0}};
static const seg_t ELECTRO_STILL[] = {{LN, 16, -46, -16, 4, 0}, {LN, -16, 4, 12, 4, 0}, {LN, 12, 4, -14, 48, 0}};
static const seg_t DENDRO_STILL[] = {
    {AR, -40, 0, 76, -58, 58}, {AR, 40, 0, 76, 122, 238}, {LN, 0, -36, 0, 88, 0},  {LN, 0, 12, 22, -12, 0},
    {LN, 0, 12, -22, -12, 0},     {LN, 0, 40, 15, 22, 0},       {LN, 0, 40, -15, 22, 0},
};
static const seg_t HYDRO_STILL[] = {
    {AR, 0, 24, 46, -40, 220}, {LN, 35, -6, 0, -74, 0}, {LN, -35, -6, 0, -74, 0}, {AR, -13, 26, 13, 180, 360}, {AR, 13, 26, 13, 0, 180},
};
static const seg_t PYRO_STILL[] = {
    {AR, 0, 26, 44, 0, 180}, {LN, -44, 26, -40, -28, 0}, {LN, -40, -28, -20, -6, 0}, {LN, -20, -6, 0, -84, 0},
    {LN, 0, -84, 20, -6, 0},    {LN, 20, -6, 40, -28, 0},   {LN, 40, -28, 44, 26, 0},   {AR, 0, 30, 15, 0, 360},
};
static const seg_t CRYO_SPIN[] = {
    {LN, 0, -16, 0, -84, 0}, {LN, 0, -54, -17, -71, 0}, {LN, 0, -54, 17, -71, 0}, {LN, 0, -30, -10, -40, 0}, {LN, 0, -30, 10, -40, 0},
};
static const seg_t CRYO_STILL[] = {{AR, 0, 0, 9, 0, 360}};

#define N(x) ((uint8_t)(sizeof(x) / sizeof((x)[0])))
static const emblem_t EMBLEM[ELEMENTS] = {
    {ANEMO_SPIN, N(ANEMO_SPIN), 3, NULL, 0},
    {NULL, 0, 0, GEO_STILL, N(GEO_STILL)},
    {ELECTRO_SPIN, N(ELECTRO_SPIN), 3, ELECTRO_STILL, N(ELECTRO_STILL)},
    {NULL, 0, 0, DENDRO_STILL, N(DENDRO_STILL)},
    {NULL, 0, 0, HYDRO_STILL, N(HYDRO_STILL)},
    {NULL, 0, 0, PYRO_STILL, N(PYRO_STILL)},
    {CRYO_SPIN, N(CRYO_SPIN), 6, CRYO_STILL, N(CRYO_STILL)},
};

typedef struct {
    lv_layer_t *layer;
    int cx, cy;
    bool dim; /* the always-on version: everything darker */
} pen_t;

static lv_color_t ink(const pen_t *p, uint32_t hex)
{
    lv_color_t c = lv_color_hex(hex);
    return p->dim ? ui_tint(c, 45) : c;
}

static void seg_draw(const pen_t *p, const seg_t *s, float k, int dy, float turn, int width, lv_color_t color, lv_opa_t opa)
{
    float cs = cosf(turn * RAD), sn = sinf(turn * RAD);
    float ox = (float)p->cx, oy = (float)(p->cy + dy);
    if (s->kind == LN) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = color, d.opa = opa, d.width = width, d.round_start = 1, d.round_end = 1;
        d.p1.x = (lv_value_precise_t)lroundf(ox + k * (s->a * cs - s->b * sn));
        d.p1.y = (lv_value_precise_t)lroundf(oy + k * (s->a * sn + s->b * cs));
        d.p2.x = (lv_value_precise_t)lroundf(ox + k * (s->c * cs - s->d * sn));
        d.p2.y = (lv_value_precise_t)lroundf(oy + k * (s->c * sn + s->d * cs));
        lv_draw_line(p->layer, &d);
    } else {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.color = color, d.opa = opa, d.width = width, d.rounded = 1;
        d.center.x = (int32_t)lroundf(ox + k * (s->a * cs - s->b * sn));
        d.center.y = (int32_t)lroundf(oy + k * (s->a * sn + s->b * cs));
        d.radius = (uint16_t)lroundf(k * s->c + width / 2.0f);
        if (s->e - s->d >= 360) {
            d.start_angle = 0, d.end_angle = 360;
        } else {
            int from = ((int)lroundf(s->d + turn) % 360 + 360) % 360;
            d.start_angle = from, d.end_angle = (from + s->e - s->d) % 360;
        }
        lv_draw_arc(p->layer, &d);
    }
}

static void emblem_pass(const pen_t *p, const emblem_t *em, float k, int dy, int width, lv_color_t color, lv_opa_t opa)
{
    for (int f = 0; f < em->fold; f++) {
        for (int i = 0; i < em->spin_n; i++) seg_draw(p, &em->spin[i], k, dy, 360.0f * f / em->fold, width, color, opa);
    }
    for (int i = 0; i < em->still_n; i++) seg_draw(p, &em->still[i], k, dy, 0, width, color, opa);
}

/* A soft dark edge first, so the white strokes read on a bright gem. */
static void emblem_draw(const pen_t *p, int element, int percent, int dy)
{
    const emblem_t *em = &EMBLEM[element];
    float k = percent / 100.0f;
    int width = (int)(k * 9.0f + 0.5f);
    if (width < 3) width = 3;
    if (p->dim) {
        emblem_pass(p, em, k, dy, width, ui_tint(lv_color_hex(ELEMENT[element].light), 62), LV_OPA_COVER);
        return;
    }
    emblem_pass(p, em, k, dy, width + 8, lv_color_hex(ELEMENT[element].deep), 90);
    emblem_pass(p, em, k, dy, width, lv_color_white(), 245);
}

/* ----------------------------------------------------------------- frames */

static void polar(const pen_t *p, float deg, float r, lv_point_precise_t *out)
{
    out->x = (lv_value_precise_t)lroundf((float)p->cx + r * cosf(deg * RAD));
    out->y = (lv_value_precise_t)lroundf((float)p->cy + r * sinf(deg * RAD));
}

/* Part of a ring round the centre: `r` is the middle of a band `w` wide. */
static void band(const pen_t *p, int r, int w, int from, int to, lv_color_t color)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = color, d.width = w, d.radius = (uint16_t)(r + w / 2);
    d.center.x = p->cx, d.center.y = p->cy;
    d.start_angle = from, d.end_angle = to;
    lv_draw_arc(p->layer, &d);
}

static void spot(const pen_t *p, float deg, float r, int size, lv_color_t color)
{
    lv_point_precise_t c;
    polar(p, deg, r, &c);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE, d.bg_color = color;
    lv_area_t a = {(int32_t)c.x - size, (int32_t)c.y - size, (int32_t)c.x + size, (int32_t)c.y + size};
    lv_draw_rect(p->layer, &d, &a);
}

/* Four-cornered shape given in (degrees, radius) pairs, going round. */
static void quad(const pen_t *p, const float pts[8], lv_color_t color)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    polar(p, pts[0], pts[1], &d.p[0]);
    polar(p, pts[2], pts[3], &d.p[1]);
    polar(p, pts[4], pts[5], &d.p[2]);
    lv_draw_triangle(p->layer, &d);
    polar(p, pts[4], pts[5], &d.p[1]);
    polar(p, pts[6], pts[7], &d.p[2]);
    lv_draw_triangle(p->layer, &d);
}

/* A diamond lying along a spoke from r0 to r1, `half` degrees wide at its waist. */
static void kite(const pen_t *p, float deg, float r0, float r1, float half, lv_color_t color)
{
    float mid = (r0 + r1) / 2;
    quad(p, (const float[8]){deg, r0, deg + half, mid, deg, r1, deg - half, mid}, color);
}

static void spoke(const pen_t *p, float deg, float r0, float r1, int w, lv_color_t color)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color, d.width = w;
    polar(p, deg, r0, &d.p1);
    polar(p, deg, r1, &d.p2);
    lv_draw_line(p->layer, &d);
}

static void frame_draw(const pen_t *p, int region)
{
    const region_t *R = &REGION[region];
    lv_color_t metal = ink(p, R->metal), accent = ink(p, R->accent);

    /* The ring itself: a dark body between two bright lips. Resting, only the lips. */
    if (!p->dim) band(p, BAND, 26, 0, 360, lv_color_hex(R->body));
    band(p, 225, 3, 0, 360, metal);
    band(p, 201, 3, 0, 360, metal);

    switch (region) {
    case 0: /* Mondstadt: a pair of wings spread from the top, and a gold stud */
        for (int side = -1; side <= 1; side += 2) {
            static const struct {
                int w, reach;
            } feather[3] = {{18, 34}, {11, 52}, {5, 70}};
            for (int i = 0; i < 3; i++) {
                int a = 270 + side * 7, b = 270 + side * feather[i].reach;
                band(p, 226 - feather[i].w / 2, feather[i].w, a < b ? a : b, a < b ? b : a, metal);
            }
        }
        spot(p, 270, BAND, 9, accent);
        spot(p, 90, BAND, 6, accent);
        break;
    case 1: /* Liyue: squared off with four large gold diamonds */
        band(p, BAND, 2, 0, 360, accent);
        for (int i = 0; i < 4; i++) {
            kite(p, 45 + i * 90, 194, 232, 4.6f, metal);
            kite(p, 45 + i * 90, 204, 222, 2.0f, ink(p, R->body));
            kite(p, i * 90, 203, 223, 2.2f, accent);
        }
        break;
    case 2: /* Inazuma: three comma swirls chasing each other */
        for (int i = 0; i < 3; i++) {
            int a = 270 + i * 120;
            band(p, BAND, 8, (a + 4) % 360, (a + 44) % 360, metal);
            band(p, BAND, 3, (a + 44) % 360, (a + 72) % 360, metal);
            spot(p, a, BAND, 11, accent);
            spot(p, a, BAND, 4, ink(p, R->body));
        }
        break;
    case 3: /* Sumeru: leaves laid end to end round the ring */
        for (int i = 0; i < 8; i++) {
            float a = 22.5f + i * 45;
            quad(p, (const float[8]){a - 14, BAND, a, BAND + 10, a + 14, BAND, a, BAND - 10}, accent);
            spot(p, i * 45, BAND, 4, metal);
        }
        break;
    case 4: /* Fontaine: clockwork, with a blue jewel at each quarter */
        for (int i = 0; i < 60; i++) {
            if (i % 5 == 0) spoke(p, i * 6, 203, 223, 4, metal);
            else spoke(p, i * 6, 207, 219, 2, ink(p, 0x8E9BB3));
        }
        for (int i = 0; i < 4; i++) {
            spot(p, i * 90, BAND, 10, metal);
            spot(p, i * 90, BAND, 7, accent);
        }
        break;
    case 5: /* Natlan: a ring of teeth, like a sun */
        for (int i = 0; i < 24; i++) {
            float a = i * 15;
            quad(p, (const float[8]){a - 5.5f, 224, a, 204, a + 5.5f, 224, a, 224}, i % 2 ? metal : accent);
        }
        break;
    default: /* Snezhnaya: an eight-pointed star of ice */
        for (int i = 0; i < 8; i++) {
            if (i % 2 == 0) kite(p, i * 45, 182, 232, 2.6f, metal);
            else kite(p, i * 45, 196, 230, 1.7f, accent);
            spot(p, 22.5f + i * 45, BAND, 3, metal);
        }
        break;
    }
}

/* ---------------------------------------------------------------- the gem */

typedef struct {
    lv_obj_t *glow, *art;
    bool ambient;
    bool home;  /* the live watch face: still while an app covers it */
    bool shade; /* darker towards the bottom, where a watch face puts the time */
    int16_t dy; /* emblem offset from the centre, and its size in percent */
    uint8_t percent;
    uint32_t t;
} vis_t;

static uint8_t *polar_r, *polar_a;
static int8_t sine[256];

static bool tables(void)
{
    if (polar_r) return true;
    polar_r = lv_malloc(GLOW_N * GLOW_N);
    polar_a = lv_malloc(GLOW_N * GLOW_N);
    if (!polar_r || !polar_a) {
        lv_free(polar_r), lv_free(polar_a);
        polar_r = polar_a = NULL;
        return false;
    }
    for (int i = 0; i < 256; i++) sine[i] = (int8_t)(127.0f * sinf(i * 6.2831853f / 256.0f));
    const float c = SCR / 8.0f;
    for (int y = 0; y < GLOW_N; y++) {
        for (int x = 0; x < GLOW_N; x++) {
            float dx = x - c, dy = y - c;
            float r = sqrtf(dx * dx + dy * dy) / c * 255.0f;
            polar_r[y * GLOW_N + x] = (uint8_t)(r > 255 ? 255 : r);
            polar_a[y * GLOW_N + x] = (uint8_t)((atan2f(dy, dx) + 3.14159265f) * (255.9f / 6.2831853f));
        }
    }
    return true;
}

static void gem_paint(vis_t *v)
{
    uint8_t *out = glow_field(v->glow);
    if (!out || !tables()) return;
    const element_t *el = &ELEMENT[V.element];

    /* Dark edge to pale heart, through the element's own colour. */
    uint8_t pal[256][3];
    for (int i = 0; i < 256; i++) {
        uint32_t a = i < 128 ? el->deep : el->mid, b = i < 128 ? el->mid : el->light;
        int f = i & 127;
        for (int c = 0; c < 3; c++) {
            int ca = (a >> (16 - 8 * c)) & 255, cb = (b >> (16 - 8 * c)) & 255;
            pal[i][c] = (uint8_t)(ca + (cb - ca) * f / 128);
        }
    }

    int t1 = (int)(v->t * 3), t2 = (int)(v->t * 2);
    int breathe = v->ambient ? 0 : sine[(v->t * 2) & 255] / 12;
    int gain = v->ambient ? 84 : 256;
    const int hx = GLOW_N / 2 - 19, hy = GLOW_N / 2 - 25, reach = 15 * 15;

    for (int y = 0; y < GLOW_N; y++) {
        for (int x = 0; x < GLOW_N; x++, out += 3) {
            int i = y * GLOW_N + x, r = polar_r[i];
            /* Awake, the gem runs on under the metal frame, which hides its
             * coarse edge. Resting there is no frame body, so it fades out. */
            if (r > (v->ambient ? GEM_R : GEM_R + 10)) {
                out[0] = out[1] = out[2] = 0;
                continue;
            }
            int a = polar_a[i], q = r * 255 / GEM_R;
            if (q > 255) q = 255;
            /* Light folding slowly round inside the glass; still at the very
             * middle, where every direction meets. */
            int fold = (sine[(a * 2 + (q >> 1) + (t1 >> 2)) & 255] * sine[(a * 3 - q + (t2 >> 2)) & 255]) >> 7;
            int core = 255 - q;
            core = (core * core) >> 8;
            int val = 58 + breathe + ((core * 118) >> 8) + ((fold * q * 3) >> 10);
            /* A dark band, then the bright edge where the glass catches light. */
            if (q > 218) val += (q - 218) * 4;
            else if (q > 168) val -= ((q - 168) * (218 - q)) >> 4;
            if (val < 0) val = 0;
            if (val > 255) val = 255;

            int c[3] = {pal[val][0], pal[val][1], pal[val][2]};
            int g = gain;
            if (v->ambient && q > 232) g = g * (255 - q) / 23;
            if (v->shade && y > GLOW_N / 2) {
                int drop = (y - GLOW_N / 2) * 7; /* fades to about half by the time's baseline */
                g = g * (256 - (drop > 130 ? 130 : drop)) >> 8;
            }
            if (!v->ambient) {
                /* The window glint, upper left. */
                int ex = x - hx, ey = y - hy, d2 = ex * ex + ey * ey * 2;
                if (d2 < reach) {
                    int h = (reach - d2) * 255 / reach;
                    h = (h * h) >> 9;
                    for (int k = 0; k < 3; k++) c[k] += ((255 - c[k]) * h) >> 8;
                }
            }
            out[0] = (uint8_t)((c[0] * g) >> 8);
            out[1] = (uint8_t)((c[1] * g) >> 8);
            out[2] = (uint8_t)((c[2] * g) >> 8);
        }
    }
    glow_show(v->glow);
}

static void art_draw(lv_event_t *e)
{
    vis_t *v = lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    pen_t pen = {.layer = lv_event_get_layer(e), .cx = a.x1 + SCR_C, .cy = a.y1 + SCR_C, .dim = v->ambient};
    frame_draw(&pen, V.region);
    emblem_draw(&pen, V.element, v->percent, v->dy);
}

static void vision_frame(lv_timer_t *t)
{
    vis_t *v = lv_timer_get_user_data(t);
    if (S.display != DISPLAY_ACTIVE || !lv_obj_is_visible(v->art)) return;
    if (v->home && shell_current()) return;
    v->t++;
    gem_paint(v);
}

static void vision_freed(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

/* Builds the gem into `parent`. `dy` and `percent` place and size the emblem. */
static void vision_create(lv_obj_t *parent, bool ambient, bool home, bool shade, int dy, int percent)
{
    vision_load();
    vis_t *v = lv_malloc_zeroed(sizeof(vis_t));
    if (!v) return;
    v->ambient = ambient, v->home = home, v->shade = shade, v->dy = (int16_t)dy, v->percent = (uint8_t)percent;
    v->t = 40;
    v->glow = glow_create(parent);
    v->art = ui_box(parent, SCR, SCR);
    lv_obj_remove_flag(v->art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v->art, art_draw, LV_EVENT_DRAW_MAIN, v);
    lv_obj_add_event_cb(v->art, vision_freed, LV_EVENT_DELETE, v);
    gem_paint(v);
    /* Slow light needs few frames: about twelve a second, and none while resting. */
    if (!ambient) ui_timer(v->art, vision_frame, 80, v);
}

void vision_face(lv_obj_t *root, bool ambient, bool home)
{
    vision_create(root, ambient, home, true, -64, 62);
}

/* ------------------------------------------------------------------- app */

static lv_obj_t *vs_scr, *vs_name, *vs_region;

static void vision_paint(void)
{
    if (!vs_scr) return;
    lv_label_set_text(vs_name, ELEMENT[V.element].name);
    lv_label_set_text(lv_obj_get_child(vs_region, 0), REGION[V.region].name);
}

static void vision_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {SCR, SCR_C};
    if (indev) lv_indev_get_point(indev, &p);
    V.element = (uint8_t)((V.element + (p.x < SCR_C ? ELEMENTS - 1 : 1)) % ELEMENTS);
    svc_kv_save("vision", &V, sizeof(V));
    vision_paint();
    bus_emit(EVT_GAME, NULL);
}

static void vision_region(lv_event_t *e)
{
    LV_UNUSED(e);
    V.region = (uint8_t)((V.region + 1) % REGIONS);
    svc_kv_save("vision", &V, sizeof(V));
    vision_paint();
    bus_emit(EVT_GAME, NULL);
}

void vision_get(int *element, int *region)
{
    vision_load();
    *element = V.element, *region = V.region;
}

/* From the phone. A value below zero leaves that one as it is. */
void vision_set(int element, int region)
{
    vision_load();
    uint8_t el = element < 0 ? V.element : (uint8_t)(element >= ELEMENTS ? ELEMENTS - 1 : element);
    uint8_t rg = region < 0 ? V.region : (uint8_t)(region >= REGIONS ? REGIONS - 1 : region);
    if (el == V.element && rg == V.region) return;
    V.element = el, V.region = rg;
    svc_kv_save("vision", &V, sizeof(V));
    vision_paint(); /* the gems themselves pick the change up on their next frame */
    bus_emit(EVT_GAME, NULL);
}

static void vision_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    vs_scr = NULL;
}

static void vision_use(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.face = FACE_VISION;
    settings_changed();
    shell_toast("Vision watch face on");
}

static void vision_app(lv_obj_t *scr)
{
    vs_scr = scr;
    lv_obj_add_event_cb(scr, vision_closed, LV_EVENT_DELETE, NULL);
    vision_create(scr, false, false, false, -14, 84);

    lv_obj_t *tap = ui_box(scr, SCR, SCR);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, vision_tap, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_align(ui_label(scr, LV_SYMBOL_LEFT, F20, C_TEXT), LV_ALIGN_LEFT_MID, 52, -14);
    lv_obj_align(ui_label(scr, LV_SYMBOL_RIGHT, F20, C_TEXT), LV_ALIGN_RIGHT_MID, -52, -14);

    vs_name = ui_label(scr, "", F20, C_TEXT);
    lv_obj_set_style_text_letter_space(vs_name, 4, 0);
    lv_obj_align(vs_name, LV_ALIGN_CENTER, 0, -132);

    vs_region = ui_pill_btn(scr, "", lv_color_black(), C_TEXT, vision_region, NULL);
    lv_obj_set_height(vs_region, 44);
    lv_obj_set_style_bg_opa(vs_region, 150, 0);
    lv_obj_align(vs_region, LV_ALIGN_CENTER, 0, 104);
    lv_obj_t *use = ui_round_btn(scr, 46, lv_color_black(), &ic_clock_28, C_TEXT, vision_use, NULL);
    lv_obj_set_style_bg_opa(use, 150, 0);
    lv_obj_align(use, LV_ALIGN_CENTER, 0, 158);
    vision_paint();
}

const app_t app_vision = {
    .id = "vision", .name = "Vision", .icon = &ic_gem_48, .color = VISION_COLOR, .create = vision_app, .no_clock = true,
};

#ifdef ORBIT_SIM
void vision_sim_set(int element, int region)
{
    vision_load();
    V.element = (uint8_t)(element % ELEMENTS), V.region = (uint8_t)(region % REGIONS);
    vision_paint();
}
#endif
