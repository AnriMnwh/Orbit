/* Photos, Camera (Lumix remote) and Maps. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../ui/ui.h"

/* ----------------------------------------------------------------- photos */

#define PHOTOS_COLOR 0xFF9966

static lv_obj_t *ph_scr, *ph_img, *ph_count, *ph_empty, *ph_bar;
static int ph_index;

static void photos_paint(void)
{
    int n = svc_photo_count();
    if (n == 0) {
        lv_obj_add_flag(ph_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ph_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ph_empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ph_count, "");
        return;
    }
    ph_index = ((ph_index % n) + n) % n;
    const lv_image_dsc_t *img = svc_photo(ph_index);
    if (img) lv_image_set_src(ph_img, img);
    lv_obj_remove_flag(ph_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ph_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ph_empty, LV_OBJ_FLAG_HIDDEN);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", ph_index + 1, n);
    lv_label_set_text(ph_count, buf);
}

static void photos_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {SCR_C, SCR_C};
    if (indev) lv_indev_get_point(indev, &p);
    ph_index += p.x < SCR_C ? -1 : 1;
    photos_paint();
}

static void photos_use_face(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.face = FACE_PHOTO;
    settings_changed();
    shell_toast("Photo watch face on");
}

static void photos_use_aod(lv_event_t *e)
{
    LV_UNUSED(e);
    CFG.aod_mode = CFG.aod_mode == AOD_PHOTOS ? AOD_CLOCK : AOD_PHOTOS;
    settings_changed();
    shell_toast(CFG.aod_mode == AOD_PHOTOS ? "Always-on photos on" : "Always-on photos off");
}

static void photos_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_PHOTOS) photos_paint();
}

static void photos_create(lv_obj_t *scr)
{
    ph_scr = scr;
    ph_img = lv_image_create(scr);
    lv_obj_center(ph_img);
    lv_obj_add_flag(ph_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ph_img, photos_tap, LV_EVENT_CLICKED, NULL);

    ph_empty = ui_box(scr, SCR, SCR);
    lv_obj_align(ui_icon(ph_empty, &ic_image_48, lv_color_hex(PHOTOS_COLOR)), LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *t = ui_label(ph_empty, "No photos yet", F28, C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 156);
    lv_obj_t *h = ui_label(ph_empty,
                           "Open the setup page on your phone to add some.\nSettings  >  Wi-Fi shows the link.", F20, C_DIM);
    lv_obj_set_width(h, 320);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 204);

    /* Controls float over the picture on a dark pill. */
    ph_bar = ui_box(scr, 232, 76);
    lv_obj_set_style_radius(ph_bar, 38, 0);
    lv_obj_set_style_bg_color(ph_bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ph_bar, 170, 0);
    lv_obj_align(ph_bar, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t *face = ui_round_btn(ph_bar, 60, C_SURF2, &ic_clock_28, C_TEXT, photos_use_face, NULL);
    lv_obj_align(face, LV_ALIGN_LEFT_MID, 8, 0);
    ph_count = ui_label(ph_bar, "", F20, C_TEXT);
    lv_obj_center(ph_count);
    lv_obj_t *aod = ui_round_btn(ph_bar, 60, C_SURF2, &ic_moon_28, C_TEXT, photos_use_aod, NULL);
    lv_obj_align(aod, LV_ALIGN_RIGHT_MID, -8, 0);

    bus_watch(scr, photos_event, NULL);
    photos_paint();
}

const app_t app_photos = {
    .id = "photos", .name = "Photos", .icon = &ic_image_48, .color = PHOTOS_COLOR, .create = photos_create,
};

/* ----------------------------------------------------------------- camera */

#define CAMERA_COLOR 0xFFC53D

static lv_obj_t *c_top, *c_sub, *c_shutter, *c_rec, *c_delay, *c_delay_label, *c_connect, *c_count;
static int c_delay_s;     /* self-timer setting: 0, 2 or 10 seconds */
static int c_countdown;   /* seconds left before the shot fires */
static int c_interval_s;  /* time-lapse interval, 0 = off */
static bool c_seq;        /* a time-lapse is running */
static int c_seq_left, c_seq_shots;
static lv_obj_t *c_interval;

static void camera_paint(void)
{
    char buf[64];
    bool ready = S.cam.status == CAM_READY;

    switch (S.cam.status) {
    case CAM_READY: snprintf(buf, sizeof(buf), "%s", S.cam.model[0] ? S.cam.model : "LUMIX"); break;
    case CAM_CONNECTING: snprintf(buf, sizeof(buf), "Connecting"); break;
    case CAM_ERROR: snprintf(buf, sizeof(buf), "Can't reach camera"); break;
    default: snprintf(buf, sizeof(buf), "LUMIX remote"); break;
    }
    ui_label_set(c_top, buf);

    if (ready) {
        char batt[16] = "", shots[24] = "";
        if (S.cam.battery >= 0) snprintf(batt, sizeof(batt), "%d%%", S.cam.battery);
        if (S.cam.shots_left >= 0) snprintf(shots, sizeof(shots), "%d left", S.cam.shots_left);
        snprintf(buf, sizeof(buf), "%s%s%s", batt, batt[0] && shots[0] ? "  " LV_SYMBOL_BULLET "  " : "", shots);
    } else if (S.cam.status == CAM_OFF) {
        snprintf(buf, sizeof(buf), "On the camera choose\nWi-Fi  >  Remote shooting");
    } else {
        snprintf(buf, sizeof(buf), "%s", S.cam.note);
    }
    ui_label_set(c_sub, buf);

    lv_obj_t *controls[] = {c_shutter, c_rec, c_delay, c_delay_label};
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        if (ready) lv_obj_remove_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ready || S.cam.status == CAM_CONNECTING) lv_obj_add_flag(c_connect, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(c_connect, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_color(c_rec, S.cam.recording ? C_BAD : C_SURF2, 0);
    ui_icon_set(ui_btn_icon(c_rec), S.cam.recording ? &ic_stop_48 : &ic_rec_48, S.cam.recording ? C_TEXT : C_BAD);
    snprintf(buf, sizeof(buf), c_delay_s ? "%ds" : "Off", c_delay_s);
    ui_label_set(c_delay_label, buf);
    if (c_countdown > 0) snprintf(buf, sizeof(buf), "%d", c_countdown);
    else if (c_seq) snprintf(buf, sizeof(buf), "%d", c_seq_shots);
    else buf[0] = 0;
    ui_label_set(c_count, buf);

    if (c_seq) snprintf(buf, sizeof(buf), "Next in %d s", c_seq_left);
    else if (c_interval_s) snprintf(buf, sizeof(buf), "Time-lapse  %d s", c_interval_s);
    else snprintf(buf, sizeof(buf), "Time-lapse off");
    ui_label_set(lv_obj_get_child(c_interval, 0), buf);
    if (ready) lv_obj_remove_flag(c_interval, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c_interval, LV_OBJ_FLAG_HIDDEN);
}

static void camera_fire(void)
{
    svc_camera(CAMCMD_SHUTTER);
    if (CFG.sound) svc_sound(SND_TAP);
}

static void camera_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (S.cam.status != CAM_READY) c_seq = false;
    if (c_seq && --c_seq_left <= 0) {
        /* Time-lapse: another frame, then wait out the interval again. */
        camera_fire();
        c_seq_shots++;
        c_seq_left = c_interval_s;
    }
    if (c_countdown > 0 && --c_countdown == 0) camera_fire();
    camera_paint();
}

static void camera_shutter(lv_event_t *e)
{
    LV_UNUSED(e);
    if (c_interval_s) {
        /* With an interval set, the shutter starts and stops a time-lapse. */
        c_seq = !c_seq;
        if (c_seq) camera_fire(), c_seq_shots = 1, c_seq_left = c_interval_s;
    } else if (c_countdown > 0) {
        c_countdown = 0; /* a second tap cancels the countdown */
    } else if (c_delay_s) {
        c_countdown = c_delay_s;
    } else {
        camera_fire();
    }
    camera_paint();
}

static void camera_interval(lv_event_t *e)
{
    LV_UNUSED(e);
    if (c_seq) return;
    c_interval_s = c_interval_s == 0 ? 5 : (c_interval_s == 5 ? 15 : (c_interval_s == 15 ? 60 : 0));
    camera_paint();
}

static void camera_record(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_camera(CAMCMD_RECORD_TOGGLE);
}

static void camera_delay(lv_event_t *e)
{
    LV_UNUSED(e);
    c_delay_s = c_delay_s == 0 ? 2 : (c_delay_s == 2 ? 10 : 0);
    camera_paint();
}

static void camera_connect(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_camera_link(true);
}

static void camera_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_CAMERA) camera_paint();
}

static void camera_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_camera_link(false);
}

static void camera_create(lv_obj_t *scr)
{
    lv_color_t c = lv_color_hex(CAMERA_COLOR);
    c_countdown = 0;

    c_top = ui_label_fit(scr, "", F24, C_TEXT, 300);
    lv_obj_align(c_top, LV_ALIGN_TOP_MID, 0, 52);
    c_sub = ui_label(scr, "", F16, C_DIM);
    lv_obj_set_width(c_sub, 320);
    lv_obj_set_style_text_align(c_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(c_sub, LV_ALIGN_TOP_MID, 0, 88);

    /* Shutter: a white disc inside a ring, like the real thing. */
    c_shutter = ui_round_btn(scr, 168, lv_color_black(), NULL, C_TEXT, camera_shutter, NULL);
    lv_obj_set_style_border_color(c_shutter, C_TEXT, 0);
    lv_obj_set_style_border_width(c_shutter, 6, 0);
    lv_obj_set_style_border_opa(c_shutter, LV_OPA_COVER, 0);
    lv_obj_align(c_shutter, LV_ALIGN_CENTER, 0, 32);
    lv_obj_t *disc = ui_box(c_shutter, 136, 136);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, C_TEXT, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_center(disc);
    c_count = ui_label(disc, "", &font_num_mid, lv_color_black());
    lv_obj_center(c_count);

    c_rec = ui_round_btn(scr, 76, C_SURF2, &ic_rec_48, C_BAD, camera_record, NULL);
    lv_obj_align(c_rec, LV_ALIGN_CENTER, -150, 32);
    c_delay = ui_round_btn(scr, 76, C_SURF2, &ic_timer_48, C_TEXT, camera_delay, NULL);
    lv_obj_align(c_delay, LV_ALIGN_CENTER, 150, 20);
    c_delay_label = ui_label(scr, "", F16, C_DIM);
    lv_obj_align(c_delay_label, LV_ALIGN_CENTER, 150, 74);

    c_connect = ui_pill_btn(scr, "Connect", c, lv_color_black(), camera_connect, NULL);
    lv_obj_align(c_connect, LV_ALIGN_CENTER, 0, 60);

    c_seq = false;
    c_interval = ui_pill_btn(scr, "", C_SURF, C_DIM, camera_interval, NULL);
    lv_obj_set_height(c_interval, 44);
    lv_obj_align(c_interval, LV_ALIGN_BOTTOM_MID, 0, -26);

    lv_obj_add_event_cb(scr, camera_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, camera_event, NULL);
    ui_timer(scr, camera_tick, 1000, NULL);
    camera_paint();
}

const app_t app_camera = {
    .id = "camera", .name = "Camera", .icon = &ic_camera_48, .color = CAMERA_COLOR,
    .create = camera_create, .keep_awake = true,
};

/* ------------------------------------------------------------------- maps */

#define MAPS_COLOR 0x9BE15D
#define TILE_PX 256
#define ZOOM_MIN 3
#define ZOOM_MAX 17
#define PI_D 3.14159265358979

static lv_obj_t *mp_view, *mp_info, *mp_follow_btn;
static double mp_lat = 20.0, mp_lon = 0.0; /* map centre */
static int mp_zoom = 14;
static bool mp_follow = true, mp_placed;

/* Web Mercator: degrees -> world pixels at a zoom level. */
static void to_world(double lat, double lon, int zoom, double *x, double *y)
{
    double n = (double)(TILE_PX * (1 << zoom));
    double rad = lat * PI_D / 180.0;
    *x = (lon + 180.0) / 360.0 * n;
    *y = (1.0 - log(tan(rad) + 1.0 / cos(rad)) / PI_D) / 2.0 * n;
}

static void from_world(double x, double y, int zoom, double *lat, double *lon)
{
    double n = (double)(TILE_PX * (1 << zoom));
    *lon = x / n * 360.0 - 180.0;
    *lat = atan(sinh(PI_D * (1.0 - 2.0 * y / n))) * 180.0 / PI_D;
}

static void maps_info(void)
{
    char buf[48];
    if (!S.gps.fix) snprintf(buf, sizeof(buf), "Waiting for phone location");
    else snprintf(buf, sizeof(buf), "%.1f km/h", (double)S.gps.speed_kmh);
    lv_label_set_text(mp_info, buf);
    lv_obj_set_style_bg_color(mp_follow_btn, mp_follow ? lv_color_hex(MAPS_COLOR) : C_SURF2, 0);
    ui_icon_set(ui_btn_icon(mp_follow_btn), NULL, mp_follow ? lv_color_black() : C_TEXT);
}

static void maps_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    double cx, cy;
    to_world(mp_lat, mp_lon, mp_zoom, &cx, &cy);
    int left = (int)floor(cx) - SCR_C, top = (int)floor(cy) - SCR_C;
    int span = 1 << mp_zoom;

    lv_draw_image_dsc_t img;
    lv_draw_image_dsc_init(&img);
    lv_draw_rect_dsc_t blank;
    lv_draw_rect_dsc_init(&blank);
    blank.bg_color = C_SURF;
    blank.border_color = C_SURF2;
    blank.border_width = 1;

    for (int ty = (int)floor((double)top / TILE_PX); ty * TILE_PX < top + SCR; ty++) {
        for (int tx = (int)floor((double)left / TILE_PX); tx * TILE_PX < left + SCR; tx++) {
            lv_area_t r = {a.x1 + tx * TILE_PX - left, a.y1 + ty * TILE_PX - top, 0, 0};
            r.x2 = r.x1 + TILE_PX - 1, r.y2 = r.y1 + TILE_PX - 1;
            const lv_image_dsc_t *tile = NULL;
            if (ty >= 0 && ty < span) tile = svc_tile(mp_zoom, ((tx % span) + span) % span, ty);
            if (tile) {
                img.src = tile;
                lv_draw_image(layer, &img, &r);
            } else {
                lv_draw_rect(layer, &blank, &r);
            }
        }
    }

    if (S.gps.fix) {
        double gx, gy;
        to_world(S.gps.lat, S.gps.lon, mp_zoom, &gx, &gy);
        int x = a.x1 + (int)(gx - left), y = a.y1 + (int)(gy - top);
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = lv_color_hex(0x58B6FF);
        d.bg_opa = 70;
        lv_area_t halo = {x - 26, y - 26, x + 26, y + 26};
        lv_draw_rect(layer, &d, &halo);
        d.bg_opa = LV_OPA_COVER;
        d.bg_color = lv_color_white();
        lv_area_t rim = {x - 11, y - 11, x + 11, y + 11};
        lv_draw_rect(layer, &d, &rim);
        d.bg_color = lv_color_hex(0x2F8FFF);
        lv_area_t dot = {x - 8, y - 8, x + 8, y + 8};
        lv_draw_rect(layer, &d, &dot);
    }
}

static void maps_drag(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t v;
    lv_indev_get_vect(indev, &v);
    if (v.x == 0 && v.y == 0) return;
    double cx, cy;
    to_world(mp_lat, mp_lon, mp_zoom, &cx, &cy);
    from_world(cx - v.x, cy - v.y, mp_zoom, &mp_lat, &mp_lon);
    if (mp_lat > 85) mp_lat = 85;
    if (mp_lat < -85) mp_lat = -85;
    if (mp_follow) {
        mp_follow = false;
        maps_info();
    }
    lv_obj_invalidate(mp_view);
}

static void maps_zoom(lv_event_t *e)
{
    int z = mp_zoom + (lv_event_get_user_data(e) ? 1 : -1);
    if (z < ZOOM_MIN || z > ZOOM_MAX) return;
    mp_zoom = z;
    lv_obj_invalidate(mp_view);
}

static void maps_recentre(lv_event_t *e)
{
    LV_UNUSED(e);
    mp_follow = !mp_follow;
    if (mp_follow && S.gps.fix) mp_lat = S.gps.lat, mp_lon = S.gps.lon;
    maps_info();
    lv_obj_invalidate(mp_view);
}

static void maps_event(evt_t e, void *data, void *user)
{
    LV_UNUSED(data);
    LV_UNUSED(user);
    if (e == EVT_GPS) {
        if (S.gps.fix && (mp_follow || !mp_placed)) mp_lat = S.gps.lat, mp_lon = S.gps.lon, mp_placed = true;
        maps_info();
        lv_obj_invalidate(mp_view);
    } else if (e == EVT_TILE) {
        lv_obj_invalidate(mp_view);
    }
}

static void maps_back(lv_event_t *e)
{
    LV_UNUSED(e);
    shell_back();
}

static void maps_closed(lv_event_t *e)
{
    LV_UNUSED(e);
    svc_gps_want(false);
}

static void maps_create(lv_obj_t *scr)
{
    if (S.gps.fix && (mp_follow || !mp_placed)) mp_lat = S.gps.lat, mp_lon = S.gps.lon, mp_placed = true;

    mp_view = ui_box(scr, SCR, SCR);
    lv_obj_add_flag(mp_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(mp_view, LV_OBJ_FLAG_GESTURE_BUBBLE); /* dragging pans, it does not go back */
    lv_obj_add_event_cb(mp_view, maps_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(mp_view, maps_drag, LV_EVENT_PRESSING, NULL);

    lv_obj_t *back = ui_round_btn(scr, 60, lv_color_black(), &ic_back_28, C_TEXT, maps_back, NULL);
    lv_obj_set_style_bg_opa(back, 190, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 22, 0);

    lv_obj_t *in = ui_round_btn(scr, 60, lv_color_black(), &ic_plus_28, C_TEXT, maps_zoom, (void *)1);
    lv_obj_set_style_bg_opa(in, 190, 0);
    lv_obj_align(in, LV_ALIGN_RIGHT_MID, -22, -40);
    lv_obj_t *out = ui_round_btn(scr, 60, lv_color_black(), &ic_minus_28, C_TEXT, maps_zoom, NULL);
    lv_obj_set_style_bg_opa(out, 190, 0);
    lv_obj_align(out, LV_ALIGN_RIGHT_MID, -22, 40);

    mp_follow_btn = ui_round_btn(scr, 60, C_SURF2, &ic_target_28, C_TEXT, maps_recentre, NULL);
    lv_obj_align(mp_follow_btn, LV_ALIGN_BOTTOM_MID, 0, -76);

    mp_info = ui_label(scr, "", F16, C_TEXT);
    lv_obj_set_style_bg_color(mp_info, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mp_info, 190, 0);
    lv_obj_set_style_pad_hor(mp_info, 14, 0);
    lv_obj_set_style_pad_ver(mp_info, 6, 0);
    lv_obj_set_style_radius(mp_info, 16, 0);
    lv_obj_align(mp_info, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* Required credit for the map data. */
    lv_obj_t *credit = ui_label(scr, "(c) OpenStreetMap", F14, C_DIM);
    lv_obj_set_style_bg_color(credit, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(credit, 170, 0);
    lv_obj_set_style_pad_hor(credit, 8, 0);
    lv_obj_set_style_radius(credit, 8, 0);
    lv_obj_align(credit, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_add_event_cb(scr, maps_closed, LV_EVENT_DELETE, NULL);
    bus_watch(scr, maps_event, NULL);
    svc_gps_want(true);
    maps_info();
}

const app_t app_maps = {
    .id = "maps", .name = "Maps", .icon = &ic_pin_48, .color = MAPS_COLOR,
    .create = maps_create, .keep_awake = true, .own_gestures = true, .no_clock = true,
};
