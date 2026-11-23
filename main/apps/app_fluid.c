/* Fluid: coloured ink in water. Drag to stir it, tilt the watch to pour it,
 * leave it alone and it keeps stirring itself.
 *
 * A small but real fluid solver runs it ("stable fluids" on a 60 x 60 grid,
 * walled in by the round edge of the screen): the ink is carried along by a
 * velocity field that is kept free of sinks and sources each frame, which is
 * what makes it curl and fold instead of just smearing. ui/glow.c stretches
 * the result up to the panel.
 */
#include <math.h>
#include <string.h>

#include "../ui/ui.h"

#define FLUID_COLOR 0x5CE1E6
#define FW 60 /* grid cells per side; a cell covers 8 x 8 pixels */
#define CELLS (FW * FW)
#define AT(x, y) ((y) * FW + (x))
#define ITERS 12     /* pressure passes per frame */
#define SWIRL 0.16f  /* how hard small eddies are kept spinning */
#define POUR 0.035f  /* how hard a tilt pulls the ink */
#define RIM_MAX 420
#define MID (SCR / 16.0f) /* the middle of the screen, in cells */
#define REACH (MID - 0.6f) /* how far the water goes */

typedef struct {
    float u[CELLS], v[CELLS], u0[CELLS], v0[CELLS]; /* velocity, in cells per frame */
    float p[CELLS], div[CELLS], curl[CELLS];
    float dye[3][CELLS], old[CELLS];
    int16_t side[4][CELLS]; /* left, right, up, down neighbour, or the cell itself at a wall */
    uint8_t wet[CELLS];     /* inside the round edge */
    uint8_t tone[CELLS * 3];
    /* The dry cells that touch water. They are kept the colour of the water
     * next to them, so ink does not darken or turn blocky against the wall. */
    struct {
        int16_t cell, from[8];
        uint8_t n;
    } rim[RIM_MAX];
    int rim_n;
} fluid_t;

typedef struct {
    const char *name;
    float rgb[5][3];
} palette_t;

static const palette_t PALETTES[] = {
    {"Aurora", {{0.1f, 1.0f, 0.75f}, {0.25f, 0.55f, 1.0f}, {0.7f, 0.3f, 1.0f}, {1.0f, 0.3f, 0.7f}, {0.2f, 0.9f, 0.4f}}},
    {"Ember", {{1.0f, 0.25f, 0.1f}, {1.0f, 0.6f, 0.1f}, {1.0f, 0.85f, 0.3f}, {0.9f, 0.1f, 0.35f}, {1.0f, 0.4f, 0.05f}}},
    {"Ocean", {{0.1f, 0.4f, 1.0f}, {0.1f, 0.85f, 1.0f}, {0.2f, 1.0f, 0.8f}, {0.35f, 0.3f, 1.0f}, {0.7f, 0.9f, 1.0f}}},
    {"Neon", {{1.0f, 0.1f, 0.6f}, {1.0f, 0.9f, 0.1f}, {0.1f, 1.0f, 0.3f}, {0.1f, 0.7f, 1.0f}, {0.6f, 0.2f, 1.0f}}},
    {"Ink", {{0.9f, 0.9f, 1.0f}, {0.6f, 0.65f, 0.8f}, {1.0f, 1.0f, 1.0f}, {0.45f, 0.5f, 0.7f}, {0.8f, 0.85f, 1.0f}}},
};
#define PALETTE_COUNT ((int)(sizeof(PALETTES) / sizeof(PALETTES[0])))

static fluid_t *fl;
static lv_obj_t *fl_glow;
static int fl_palette;
static float fl_hue;
static uint32_t fl_touched, fl_stirred, fl_rand;

static uint32_t rnd(void)
{
    fl_rand ^= fl_rand << 13, fl_rand ^= fl_rand >> 17, fl_rand ^= fl_rand << 5;
    return fl_rand;
}

/* The next colour: the palette's colours, blended one into the next. */
static void colour(float rgb[3], float step)
{
    fl_hue += step;
    if (fl_hue >= 5.0f) fl_hue -= 5.0f;
    int k = (int)fl_hue;
    float f = fl_hue - (float)k;
    const float *a = PALETTES[fl_palette].rgb[k % 5], *b = PALETTES[fl_palette].rgb[(k + 1) % 5];
    for (int c = 0; c < 3; c++) rgb[c] = a[c] + (b[c] - a[c]) * f;
}

/* Drop ink at (x, y), in cells, and give the water there a shove. */
static void splat(float x, float y, float dx, float dy, const float rgb[3], float amount, float radius)
{
    int x0 = (int)(x - 2.5f * radius), x1 = (int)(x + 2.5f * radius) + 1;
    int y0 = (int)(y - 2.5f * radius), y1 = (int)(y + 2.5f * radius) + 1;
    if (x0 < 1) x0 = 1;
    if (y0 < 1) y0 = 1;
    if (x1 > FW - 2) x1 = FW - 2;
    if (y1 > FW - 2) y1 = FW - 2;
    float k = 1.0f / (radius * radius);
    for (int yy = y0; yy <= y1; yy++) {
        for (int xx = x0; xx <= x1; xx++) {
            int i = AT(xx, yy);
            if (!fl->wet[i]) continue;
            float ex = (float)xx - x, ey = (float)yy - y;
            float w = expf(-(ex * ex + ey * ey) * k);
            fl->u[i] += dx * w, fl->v[i] += dy * w;
            for (int c = 0; c < 3; c++) fl->dye[c][i] += rgb[c] * amount * w;
        }
    }
}

/* Carry `src` along the flow into `dst`: each cell takes the value found
 * where the water in it was one frame ago. */
static void advect(float *dst, const float *src, const float *vx, const float *vy, float keep, float limit)
{
    for (int y = 1; y < FW - 1; y++) {
        for (int x = 1; x < FW - 1; x++) {
            int i = AT(x, y);
            if (!fl->wet[i]) continue;
            float sx = (float)x - vx[i], sy = (float)y - vy[i];
            /* Nothing comes from beyond the wall. */
            float ox = sx - MID, oy = sy - MID, far = ox * ox + oy * oy;
            if (far > REACH * REACH) {
                float pull = REACH / sqrtf(far);
                sx = MID + ox * pull, sy = MID + oy * pull;
            }
            int ix = (int)sx, iy = (int)sy;
            float fx = sx - (float)ix, fy = sy - (float)iy;
            const float *s = src + AT(ix, iy);
            float top = s[0] + (s[1] - s[0]) * fx, bottom = s[FW] + (s[FW + 1] - s[FW]) * fx;
            float val = (top + (bottom - top) * fy) * keep;
            dst[i] = val > limit ? limit : (val < -limit ? -limit : val);
        }
    }
}

/* Remove sinks and sources from the velocity, so the water only swirls. */
static void project(void)
{
    fluid_t *f = fl;
    for (int i = FW; i < CELLS - FW; i++) {
        if (!f->wet[i]) continue;
        f->div[i] = -0.5f * (f->u[i + 1] - f->u[i - 1] + f->v[i + FW] - f->v[i - FW]);
        f->p[i] = 0;
    }
    const int16_t *l = f->side[0], *r = f->side[1], *up = f->side[2], *dn = f->side[3];
    for (int k = 0; k < ITERS; k++) {
        for (int i = FW; i < CELLS - FW; i++) {
            if (f->wet[i]) f->p[i] = (f->div[i] + f->p[l[i]] + f->p[r[i]] + f->p[up[i]] + f->p[dn[i]]) * 0.25f;
        }
    }
    for (int i = FW; i < CELLS - FW; i++) {
        if (!f->wet[i]) continue;
        f->u[i] -= 0.5f * (f->p[r[i]] - f->p[l[i]]);
        f->v[i] -= 0.5f * (f->p[dn[i]] - f->p[up[i]]);
    }
}

static void fluid_step(void)
{
    fluid_t *f = fl;

    /* Tilting the watch pours the ink downhill; a level wrist leaves it be. */
    float gx, gy, gz;
    svc_tilt(&gx, &gy, &gz);
    float px = fabsf(gx) > 0.15f ? gx * POUR : 0, py = fabsf(gy) > 0.15f ? -gy * POUR : 0;

    /* Small eddies die out on a grid this coarse, so find where the water is
     * spinning and push it round a little harder. */
    for (int i = FW; i < CELLS - FW; i++) {
        if (f->wet[i]) f->curl[i] = 0.5f * ((f->v[i + 1] - f->v[i - 1]) - (f->u[i + FW] - f->u[i - FW]));
    }
    for (int i = FW; i < CELLS - FW; i++) {
        if (!f->wet[i]) continue;
        float nx = fabsf(f->curl[i + 1]) - fabsf(f->curl[i - 1]);
        float ny = fabsf(f->curl[i + FW]) - fabsf(f->curl[i - FW]);
        float w = f->curl[i] * SWIRL / (sqrtf(nx * nx + ny * ny) + 1e-4f);
        float ink = f->dye[0][i] + f->dye[1][i] + f->dye[2][i];
        f->u[i] += ny * w + px * ink;
        f->v[i] += -nx * w + py * ink;
    }

    memcpy(f->u0, f->u, sizeof(f->u));
    memcpy(f->v0, f->v, sizeof(f->v));
    advect(f->u, f->u0, f->u0, f->v0, 0.997f, 5.0f);
    advect(f->v, f->v0, f->u0, f->v0, 0.997f, 5.0f);
    project();
    for (int c = 0; c < 3; c++) {
        for (int k = 0; k < f->rim_n; k++) {
            float sum = 0;
            for (int j = 0; j < f->rim[k].n; j++) sum += f->dye[c][f->rim[k].from[j]];
            f->dye[c][f->rim[k].cell] = sum / (float)f->rim[k].n;
        }
        memcpy(f->old, f->dye[c], sizeof(f->old));
        advect(f->dye[c], f->old, f->u, f->v, 0.996f, 3.0f);
    }
}

/* Left alone, it drops ink and stirs by itself. */
static void fluid_stir(void)
{
    float a = (float)(rnd() % 628) / 100.0f, r = 5.0f + (float)(rnd() % 15);
    float b = a + 1.3f + (float)(rnd() % 100) / 110.0f; /* mostly round the middle, a little inwards */
    float rgb[3];
    colour(rgb, 0.45f);
    float c = MID;
    splat(c + cosf(a) * r, c + sinf(a) * r, cosf(b) * 2.6f, sinf(b) * 2.6f, rgb, 2.0f, 2.8f);
}

static void fluid_render(void)
{
    uint8_t *out = glow_field(fl_glow);
    if (!out) return;
    /* Ink strength to brightness, rolling off so thick ink does not burn out. */
    for (int c = 0; c < 3; c++) {
        const float *d = fl->dye[c];
        uint8_t *t = fl->tone + c;
        for (int i = 0; i < CELLS; i++, t += 3) {
            float q = 1.0f / (1.0f + (d[i] > 0 ? d[i] * 1.6f : 0));
            *t = (uint8_t)(255.0f * (1.0f - q * q));
        }
    }
    /* Twice up to the field; glow_show() does the remaining four times. */
    for (int y = 0; y < GLOW_N; y++) {
        const uint8_t *r0 = fl->tone + (y >> 1) * FW * 3, *r1 = r0 + ((y & 1) ? FW * 3 : 0);
        for (int x = 0; x < GLOW_N; x++, out += 3) {
            const uint8_t *a = r0 + (x >> 1) * 3, *b = r1 + (x >> 1) * 3;
            int s = (x & 1) ? 3 : 0;
            out[0] = (uint8_t)((a[0] + a[s] + b[0] + b[s] + 2) >> 2);
            out[1] = (uint8_t)((a[1] + a[1 + s] + b[1] + b[1 + s] + 2) >> 2);
            out[2] = (uint8_t)((a[2] + a[2 + s] + b[2] + b[2 + s] + 2) >> 2);
        }
    }
    glow_show(fl_glow);
}

static void fluid_frame(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!fl) return;
    uint32_t now = svc_millis();
    if (now - fl_touched > 2500 && now - fl_stirred > 700) {
        fl_stirred = now;
        fluid_stir();
    }
    fluid_step();
    fluid_render();
}

static void fluid_drag(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !fl) return;
    lv_point_t p, v = {0, 0};
    lv_indev_get_point(indev, &p);
    if (lv_event_get_code(e) == LV_EVENT_PRESSING) lv_indev_get_vect(indev, &v);
    float dx = (float)v.x / 8.0f, dy = (float)v.y / 8.0f; /* cells moved since the last reading */
    float speed = sqrtf(dx * dx + dy * dy);
    float rgb[3];
    colour(rgb, 0.03f + speed * 0.03f);
    /* A resting finger seeps a little ink; a moving one lays down a ribbon. */
    splat((float)p.x / 8.0f, (float)p.y / 8.0f, dx * 0.8f, dy * 0.8f, rgb, 0.12f + (speed > 1.5f ? 0.6f : speed * 0.4f), 2.2f);
    fl_touched = svc_millis();
}

static void fluid_next(lv_event_t *e)
{
    LV_UNUSED(e);
    fl_palette = (fl_palette + 1) % PALETTE_COUNT;
    shell_toast(PALETTES[fl_palette].name);
}

static void fluid_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_free(fl);
    fl = NULL;
}

static void fluid_create(lv_obj_t *scr)
{
    fl_glow = glow_create(scr);
    fl = lv_malloc_zeroed(sizeof(fluid_t));
    lv_obj_add_event_cb(scr, fluid_closed, LV_EVENT_DELETE, NULL);
    if (!fl || !glow_field(fl_glow)) {
        lv_obj_center(ui_label(scr, "Not enough memory", F20, C_DIM));
        return;
    }

    /* Wall the grid in with the round edge of the screen. */
    for (int y = 1; y < FW - 1; y++) {
        for (int x = 1; x < FW - 1; x++) {
            float dx = (float)x - MID, dy = (float)y - MID;
            fl->wet[AT(x, y)] = dx * dx + dy * dy < REACH * REACH;
        }
    }
    for (int y = 0; y < FW; y++) {
        for (int x = 0; x < FW; x++) {
            if (fl->wet[AT(x, y)] || fl->rim_n >= RIM_MAX) continue;
            int n = 0;
            for (int k = 0; k < 9; k++) {
                int nx = x + k % 3 - 1, ny = y + k / 3 - 1;
                if (nx < 0 || ny < 0 || nx >= FW || ny >= FW || !fl->wet[AT(nx, ny)]) continue;
                fl->rim[fl->rim_n].from[n++] = (int16_t)AT(nx, ny);
            }
            if (!n) continue;
            fl->rim[fl->rim_n].cell = (int16_t)AT(x, y);
            fl->rim[fl->rim_n++].n = (uint8_t)n;
        }
    }
    for (int i = FW; i < CELLS - FW; i++) {
        if (!fl->wet[i]) continue;
        fl->side[0][i] = (int16_t)(fl->wet[i - 1] ? i - 1 : i);
        fl->side[1][i] = (int16_t)(fl->wet[i + 1] ? i + 1 : i);
        fl->side[2][i] = (int16_t)(fl->wet[i - FW] ? i - FW : i);
        fl->side[3][i] = (int16_t)(fl->wet[i + FW] ? i + FW : i);
    }

    /* Open on a picture rather than a black screen. */
    fl_rand = 0x2F6E2B1u, fl_hue = 0;
    for (int i = 0; i < 6; i++) fluid_stir();
    for (int i = 0; i < 8; i++) fluid_step();
    fl_touched = 0, fl_stirred = svc_millis();

    lv_obj_t *view = ui_box(scr, SCR, SCR);
    lv_obj_add_flag(view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(view, LV_OBJ_FLAG_GESTURE_BUBBLE); /* dragging stirs, it does not go back */
    lv_obj_add_event_cb(view, fluid_drag, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(view, fluid_drag, LV_EVENT_PRESSING, NULL);

    lv_obj_t *pal = ui_round_btn(scr, 48, lv_color_black(), &ic_drop_28, C_TEXT, fluid_next, NULL);
    lv_obj_set_style_bg_opa(pal, 150, 0);
    lv_obj_align(pal, LV_ALIGN_BOTTOM_MID, 0, -12);

    ui_timer(scr, fluid_frame, 33, NULL);
    fluid_render();
}

const app_t app_fluid = {
    .id = "fluid", .name = "Fluid", .icon = &ic_wave_48, .color = FLUID_COLOR,
    .create = fluid_create, .keep_awake = true, .own_gestures = true, .no_clock = true,
};

#ifdef ORBIT_SIM
/* A drag across the middle, for the simulator's screenshots. */
void fluid_sim_stroke(void)
{
    float rgb[3];
    for (int i = 0; i < 26; i++) {
        float t = (float)i / 25.0f;
        colour(rgb, 0.08f);
        splat(10.0f + 38.0f * t, 36.0f - 14.0f * sinf(t * 3.14159f), 1.8f, -0.9f * cosf(t * 3.14159f), rgb, 0.7f, 2.2f);
        fluid_step();
    }
}
#endif
