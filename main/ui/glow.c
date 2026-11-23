/* A full-screen picture painted by hand. The caller fills a small RGB field
 * (GLOW_N x GLOW_N); glow_show() stretches it four times up to the panel,
 * smoothing between cells and dithering so dark gradients stay clean in
 * 16-bit colour. Used where LVGL widgets cannot make the picture: moving
 * light, liquid, glass. */
#include <math.h>
#include <string.h>

#include "ui.h"

typedef struct {
    uint8_t *field;  /* GLOW_N * GLOW_N * 3, RGB */
    uint16_t *frame; /* SCR * SCR, RGB565 */
    uint8_t *rows;   /* two stretched rows, SCR * 3 each */
    lv_image_dsc_t dsc;
} glow_t;

static void glow_freed(lv_event_t *e)
{
    glow_t *g = lv_event_get_user_data(e);
    lv_free(g->field), lv_free(g->frame), lv_free(g->rows);
    lv_free(g);
}

lv_obj_t *glow_create(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_center(img);
    glow_t *g = lv_malloc_zeroed(sizeof(glow_t));
    if (!g) return img;
    g->field = lv_malloc(GLOW_N * GLOW_N * 3);
    g->frame = lv_malloc(SCR * SCR * 2);
    g->rows = lv_malloc(SCR * 3 * 2);
    lv_obj_add_event_cb(img, glow_freed, LV_EVENT_DELETE, g);
    if (!g->field || !g->frame || !g->rows) return img; /* out of memory: stays blank */

    memset(g->field, 0, GLOW_N * GLOW_N * 3);
    memset(g->frame, 0, SCR * SCR * 2);
    g->dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)g->frame,
    };
    lv_obj_set_user_data(img, g);
    lv_image_set_src(img, &g->dsc);
    return img;
}

uint8_t *glow_field(lv_obj_t *glow)
{
    glow_t *g = lv_obj_get_user_data(glow);
    return g ? g->field : NULL;
}

/* Stretch one field row to SCR pixels wide. */
static void stretch_row(const uint8_t *src, uint8_t *dst)
{
    for (int x = 0; x < SCR; x++, dst += 3) {
        const uint8_t *p = src + (x >> 2) * 3;
        int f = x & 3;
        dst[0] = (uint8_t)((p[0] * (4 - f) + p[3] * f) >> 2);
        dst[1] = (uint8_t)((p[1] * (4 - f) + p[4] * f) >> 2);
        dst[2] = (uint8_t)((p[2] * (4 - f) + p[5] * f) >> 2);
    }
}

void glow_show(lv_obj_t *glow)
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    glow_t *g = lv_obj_get_user_data(glow);
    if (!g) return;
    uint8_t *ra = g->rows, *rb = g->rows + SCR * 3;
    int have = -1;
    for (int y = 0; y < SCR; y++) {
        int y0 = y >> 2, f = y & 3;
        if (y0 != have) {
            if (y0 == have + 1 && have >= 0) memcpy(ra, rb, SCR * 3);
            else stretch_row(g->field + y0 * GLOW_N * 3, ra);
            stretch_row(g->field + (y0 + 1) * GLOW_N * 3, rb);
            have = y0;
        }
        /* Only the part of the row inside the round panel is visible. */
        int dy = y - SCR_C;
        int half = (int)sqrtf((float)(SCR_C * SCR_C - dy * dy)) + 1;
        int x0 = SCR_C - half < 0 ? 0 : SCR_C - half, x1 = SCR_C + half > SCR ? SCR : SCR_C + half;
        uint16_t *dst = g->frame + y * SCR;
        const uint8_t *pa = ra + x0 * 3, *pb = rb + x0 * 3;
        for (int x = x0; x < x1; x++, pa += 3, pb += 3) {
            int r = (pa[0] * (4 - f) + pb[0] * f) >> 2;
            int gr = (pa[1] * (4 - f) + pb[1] * f) >> 2;
            int b = (pa[2] * (4 - f) + pb[2] * f) >> 2;
            if ((r | gr | b) == 0) {
                dst[x] = 0;
                continue;
            }
            int n = bayer[y & 3][x & 3];
            r += n >> 1, gr += n >> 2, b += n >> 1;
            if (r > 255) r = 255;
            if (gr > 255) gr = 255;
            if (b > 255) b = 255;
            dst[x] = (uint16_t)(((r & 0xF8) << 8) | ((gr & 0xFC) << 3) | (b >> 3));
        }
    }
    lv_obj_invalidate(glow);
}
