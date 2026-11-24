/* Assistant: voice in, voice out, with a living ring of light around the
 * bezel that shows what it is doing.
 *
 * The glow is rendered by hand rather than with LVGL widgets: a 118 x 118
 * field is computed each frame (cheap), then smoothed up to the full panel
 * with dithering so the dark gradients stay clean in 16-bit colour.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

#define AGENT_COLOR 0xB69CFF
#define LO 118 /* low-res field; LO * 4 covers the 466 px panel */

typedef struct {
    uint8_t key[4][3]; /* four colours spaced around the ring */
    int reach;         /* how far the glow leans in from the edge (0..255) */
    int speed;
    int gain;          /* overall brightness (0..256) */
} look_t;

static const look_t LOOKS[] = {
    [AGENT_IDLE] = {{{40, 70, 230}, {120, 70, 240}, {60, 120, 255}, {150, 90, 255}}, 44, 1, 150},
    [AGENT_LISTENING] = {{{0, 200, 255}, {60, 110, 255}, {170, 90, 255}, {40, 230, 220}}, 62, 2, 256},
    [AGENT_THINKING] = {{{190, 90, 255}, {255, 80, 200}, {90, 110, 255}, {255, 130, 230}}, 74, 5, 256},
    [AGENT_SPEAKING] = {{{255, 110, 170}, {255, 160, 70}, {190, 100, 255}, {255, 90, 120}}, 58, 3, 256},
    [AGENT_ERROR] = {{{255, 70, 60}, {255, 130, 50}, {220, 50, 90}, {255, 100, 60}}, 46, 1, 220},
};

static lv_obj_t *a_scr, *a_img, *a_hint, *a_text, *a_heard, *a_spark;
static uint8_t *field;         /* LO*LO*3, RGB */
static uint8_t *polar_r;       /* LO*LO, 0 centre .. 255 edge and beyond */
static uint8_t *polar_a;       /* LO*LO, angle 0..255 */
static uint16_t *frame;        /* SCR*SCR, RGB565 */
static uint8_t *rows;          /* two horizontally upscaled rows, SCR*3 each */
static lv_image_dsc_t frame_dsc;
static int8_t sine[256];
static uint16_t span_x0[SCR], span_x1[SCR];
static bool listen_on_open;

/* Eased towards the look of the current state, so changes glide. */
static float cur_key[4][3], cur_reach, cur_gain, cur_level;
static uint32_t t1, t2, t3, hue;

static bool agent_alloc(void)
{
    field = lv_malloc(LO * LO * 3);
    polar_r = lv_malloc(LO * LO);
    polar_a = lv_malloc(LO * LO);
    frame = lv_malloc(SCR * SCR * 2);
    rows = lv_malloc(SCR * 3 * 2);
    if (!field || !polar_r || !polar_a || !frame || !rows) return false;

    for (int i = 0; i < 256; i++) sine[i] = (int8_t)(127.0f * sinf(i * 6.2831853f / 256.0f));
    for (int y = 0; y < SCR; y++) {
        int dy = y - SCR_C;
        int half = (int)sqrtf((float)(SCR_C * SCR_C - dy * dy)) + 1;
        span_x0[y] = (uint16_t)(SCR_C - half < 0 ? 0 : SCR_C - half);
        span_x1[y] = (uint16_t)(SCR_C + half > SCR ? SCR : SCR_C + half);
    }
    /* Field cell (x, y) lands on panel pixel (4x, 4y), so the panel centre and
     * radius are both 233 / 4 cells. */
    const float c = SCR / 8.0f;
    for (int y = 0; y < LO; y++) {
        for (int x = 0; x < LO; x++) {
            float dx = x - c, dy = y - c;
            float r = sqrtf(dx * dx + dy * dy) / c * 255.0f;
            polar_r[y * LO + x] = (uint8_t)(r > 255 ? 255 : r);
            polar_a[y * LO + x] = (uint8_t)((atan2f(dy, dx) + 3.14159265f) * (255.9f / 6.2831853f));
        }
    }
    memset(frame, 0, SCR * SCR * 2);
    frame_dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SCR, .h = SCR, .stride = SCR * 2},
        .data_size = SCR * SCR * 2,
        .data = (const uint8_t *)frame,
    };
    return true;
}

static void agent_free(void)
{
    lv_free(field), lv_free(polar_r), lv_free(polar_a), lv_free(frame), lv_free(rows);
    field = polar_r = polar_a = rows = NULL;
    frame = NULL;
}

static void render_field(void)
{
    agent_status_t st = S.agent.status <= AGENT_ERROR ? S.agent.status : AGENT_IDLE;
    const look_t *L = &LOOKS[st];

    /* Ease colours and shape towards the target look. */
    for (int k = 0; k < 4; k++) {
        for (int c = 0; c < 3; c++) cur_key[k][c] += (L->key[k][c] - cur_key[k][c]) * 0.12f;
    }
    float level = S.agent.level < 0 ? 0 : (S.agent.level > 1 ? 1 : S.agent.level);
    cur_level += (level - cur_level) * 0.35f;
    float reach_target = (float)L->reach;
    if (st == AGENT_LISTENING || st == AGENT_SPEAKING) reach_target += cur_level * 96.0f;
    if (st == AGENT_THINKING) reach_target += 14.0f * sinf(t1 * 0.035f);
    cur_reach += (reach_target - cur_reach) * 0.25f;
    cur_gain += (L->gain - cur_gain) * 0.12f;

    t1 += (uint32_t)(3 * L->speed), t2 += (uint32_t)(2 * L->speed), t3 += (uint32_t)L->speed;
    hue += (uint32_t)L->speed;

    /* 256-entry palette from the four key colours. */
    uint8_t pal[256][3];
    for (int i = 0; i < 256; i++) {
        int k = i >> 6, f = i & 63;
        for (int c = 0; c < 3; c++) {
            float a = cur_key[k][c], b = cur_key[(k + 1) & 3][c];
            pal[i][c] = (uint8_t)(a + (b - a) * f / 64.0f);
        }
    }

    int reach = (int)cur_reach, gain = (int)cur_gain;
    uint8_t *out = field;
    for (int i = 0; i < LO * LO; i++, out += 3) {
        int r = polar_r[i];
        if (r >= 255) {
            out[0] = out[1] = out[2] = 0;
            continue;
        }
        int a = polar_a[i];
        int wave = (sine[(a * 3 + (t1 >> 2)) & 255] + sine[(a * 5 - (t2 >> 2)) & 255] + sine[(a * 2 + (t3 >> 1)) & 255]) / 3;
        int inner = 255 - ((reach * (150 + wave)) >> 8);
        int d = r - inner;
        if (d <= 0) {
            out[0] = out[1] = out[2] = 0;
            continue;
        }
        int v = d * 255 / (256 - inner);
        v = (v * v) >> 8;        /* soft inner edge */
        v = (v * gain) >> 8;
        const uint8_t *p = pal[(a + (hue >> 2) + (wave >> 2)) & 255];
        out[0] = (uint8_t)((p[0] * v) >> 8);
        out[1] = (uint8_t)((p[1] * v) >> 8);
        out[2] = (uint8_t)((p[2] * v) >> 8);
    }
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

static void render_frame(void)
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    uint8_t *ra = rows, *rb = rows + SCR * 3;
    int have = -1;
    for (int y = 0; y < SCR; y++) {
        int y0 = y >> 2, f = y & 3;
        if (y0 != have) {
            if (y0 == have + 1 && have >= 0) memcpy(ra, rb, SCR * 3);
            else stretch_row(field + y0 * LO * 3, ra);
            stretch_row(field + (y0 + 1) * LO * 3, rb);
            have = y0;
        }
        int x0 = span_x0[y], x1 = span_x1[y];
        uint16_t *dst = frame + y * SCR;
        const uint8_t *pa = ra + x0 * 3, *pb = rb + x0 * 3;
        for (int x = x0; x < x1; x++, pa += 3, pb += 3) {
            if ((pa[0] | pa[1] | pa[2] | pb[0] | pb[1] | pb[2]) == 0) {
                dst[x] = 0;
                continue;
            }
            int r = (pa[0] * (4 - f) + pb[0] * f) >> 2;
            int g = (pa[1] * (4 - f) + pb[1] * f) >> 2;
            int b = (pa[2] * (4 - f) + pb[2] * f) >> 2;
            int n = bayer[y & 3][x & 3];
            r += n >> 1, g += n >> 2, b += n >> 1;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            dst[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}

static void agent_paint_text(void)
{
    const char *hint = "", *text = "";
    bool spark = false;
    switch (S.agent.status) {
    case AGENT_IDLE:
        hint = "Tap to talk";
        if (S.agent.reply[0]) text = S.agent.reply;
        else spark = true, text = "Ask me anything";
        break;
    case AGENT_LISTENING: hint = "Tap when you're done", text = "Listening"; break;
    case AGENT_THINKING: hint = "Tap to cancel", text = "Thinking"; break;
    case AGENT_SPEAKING: hint = "Tap to stop", text = S.agent.reply; break;
    case AGENT_ERROR: hint = "Tap to try again", text = S.agent.error[0] ? S.agent.error : "Something went wrong"; break;
    }
    lv_label_set_text(a_hint, hint);
    lv_label_set_text(a_text, text);
    /* Long answers drop a size so more fits. */
    lv_obj_set_style_text_font(a_text, strlen(text) > 90 ? F20 : F24, 0);
    bool show_heard = (S.agent.status == AGENT_THINKING || S.agent.status == AGENT_SPEAKING) && S.agent.heard[0];
    lv_label_set_text(a_heard, show_heard ? S.agent.heard : "");
    if (spark) lv_obj_remove_flag(a_spark, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(a_spark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(lv_obj_get_parent(a_text), 0, LV_ANIM_OFF);
}

static void agent_frame(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!frame) return;
    render_field();
    render_frame();
    lv_obj_invalidate(a_img);
}

static void agent_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    switch (S.agent.status) {
    case AGENT_IDLE:
    case AGENT_ERROR: svc_agent_listen(true); break;
    case AGENT_LISTENING: svc_agent_listen(false); break;
    default: svc_agent_cancel(); break;
    }
}

static void agent_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_AGENT) agent_paint_text();
}

static void agent_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.agent.status != AGENT_IDLE) svc_agent_cancel();
    a_scr = NULL;
    agent_free();
}

/* Called by the shell's long-press shortcut: open and start listening at once. */
void agent_listen_on_open(void)
{
    listen_on_open = true;
}

static void agent_create(lv_obj_t *scr)
{
    a_scr = scr;
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, agent_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, agent_closed, LV_EVENT_DELETE, NULL);

    a_img = lv_image_create(scr);
    lv_obj_add_flag(a_img, LV_OBJ_FLAG_EVENT_BUBBLE);
    if (agent_alloc()) {
        memset(cur_key, 0, sizeof(cur_key));
        cur_reach = 0, cur_gain = 0, cur_level = 0;
        lv_image_set_src(a_img, &frame_dsc);
        lv_obj_center(a_img);
        ui_timer(scr, agent_frame, 45, NULL);
    } else {
        agent_free(); /* out of memory: the app still works, just without the glow */
    }

    a_heard = ui_label(scr, "", F16, C_DIM);
    lv_label_set_long_mode(a_heard, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(a_heard, 250, 2 * lv_font_get_line_height(F16));
    lv_obj_set_style_text_align(a_heard, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a_heard, LV_ALIGN_CENTER, 0, -112);

    a_spark = ui_icon(scr, &ic_spark_48, lv_color_hex(AGENT_COLOR));
    lv_obj_align(a_spark, LV_ALIGN_CENTER, 0, -54);

    /* Answers can be long, so the text scrolls inside the dark middle. */
    lv_obj_t *box = ui_box(scr, 290, 170);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 18);
    lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    a_text = ui_label(box, "", F24, C_TEXT);
    lv_obj_set_width(a_text, 290);
    lv_obj_set_style_text_align(a_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(a_text, LV_OBJ_FLAG_EVENT_BUBBLE);

    a_hint = ui_label(scr, "", F16, C_DIM);
    lv_obj_align(a_hint, LV_ALIGN_CENTER, 0, 128);

    bus_watch(scr, agent_event, NULL);
    agent_paint_text();
    if (listen_on_open && (S.agent.status == AGENT_IDLE || S.agent.status == AGENT_ERROR)) svc_agent_listen(true);
    listen_on_open = false;
}

const app_t app_agent = {
    .id = "agent", .name = "Assistant", .icon = &ic_spark_48, .color = AGENT_COLOR,
    .create = agent_create, .keep_awake = true, .no_clock = true,
};
