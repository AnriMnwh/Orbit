/* Watch entry point and the board's own chips.
 *
 * Display, touch and audio come from Waveshare's board package. This file adds
 * what that package leaves out: the power chip (battery, power key), the
 * motion sensor (raise to wake, steps), the clock chip and the side button.
 */
#include <math.h>
#include <string.h>
#include <sys/time.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qmi8658.h"

#include "platform.h"

static const char *TAG = "board";

#define SIDE_BUTTON GPIO_NUM_0 /* the BOOT key */
#define AXP2101_ADDR 0x34
#define PCF85063_ADDR 0x51
#define TIME_VALID_AFTER 1750000000 /* any real time is later than mid-2025 */

static i2c_master_dev_handle_t pmic, rtc;
static qmi8658_dev_t imu;
static bool have_imu;

/* ------------------------------------------------------------- UI thread */

void ui_post(void (*fn)(void *arg), void *arg)
{
    if (bsp_display_lock(3000) != ESP_OK) {
        ESP_LOGW(TAG, "UI busy, dropped an update");
        return;
    }
    lv_async_call(fn, arg);
    bsp_display_unlock();
    esp_lv_adapter_request_wake();
}

/* -------------------------------------------------------------------- I2C */

static i2c_master_dev_handle_t i2c_attach(uint8_t addr, const char *name)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        ESP_LOGW(TAG, "%s not found at 0x%02x", name, addr);
        return NULL;
    }
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return NULL;
    ESP_LOGI(TAG, "%s found at 0x%02x", name, addr);
    return dev;
}

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, n, 100);
}

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    uint8_t b[2] = {reg, value};
    return i2c_master_transmit(dev, b, 2, 100);
}

/* ------------------------------------------------------------------ clock */

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static void rtc_load(void)
{
    uint8_t b[7];
    if (!rtc || reg_read(rtc, 0x04, b, 7) != ESP_OK) return;
    if (b[0] & 0x80) { /* the chip lost power, so its time means nothing */
        ESP_LOGW(TAG, "clock chip has no valid time");
        return;
    }
    struct tm t = {
        .tm_sec = bcd(b[0] & 0x7F), .tm_min = bcd(b[1] & 0x7F), .tm_hour = bcd(b[2] & 0x3F),
        .tm_mday = bcd(b[3] & 0x3F), .tm_mon = bcd(b[5] & 0x1F) - 1, .tm_year = bcd(b[6]) + 100,
    };
    struct timeval tv = {.tv_sec = mktime(&t)}; /* TZ is unset, so mktime works in UTC */
    if (tv.tv_sec > TIME_VALID_AFTER) settimeofday(&tv, NULL);
}

static void rtc_store(time_t utc)
{
    if (!rtc) return;
    struct tm t;
    gmtime_r(&utc, &t);
    uint8_t b[8] = {0x04, to_bcd(t.tm_sec), to_bcd(t.tm_min), to_bcd(t.tm_hour), to_bcd(t.tm_mday),
                    (uint8_t)t.tm_wday, to_bcd(t.tm_mon + 1), to_bcd(t.tm_year % 100)};
    i2c_master_transmit(rtc, b, sizeof(b), 100);
}

typedef struct {
    int tz_minutes;
    bool tz_known;
} clock_msg_t;

static void clock_apply(void *arg)
{
    clock_msg_t *m = arg;
    if (m->tz_known && CFG.tz_minutes != m->tz_minutes) {
        CFG.tz_minutes = (int16_t)m->tz_minutes;
        settings_changed();
    }
    bus_emit(EVT_MINUTE, NULL);
    free(m);
}

void clock_set(time_t utc, int tz_minutes, bool tz_known)
{
    if (utc > TIME_VALID_AFTER) {
        struct timeval tv = {.tv_sec = utc};
        settimeofday(&tv, NULL);
        rtc_store(utc);
    }
    clock_msg_t *m = malloc(sizeof(*m));
    if (!m) return;
    *m = (clock_msg_t){tz_minutes, tz_known};
    ui_post(clock_apply, m);
}

void svc_set_time(time_t utc)
{
    clock_set(utc, 0, false);
}

/* Back to how it left the factory: settings, pairings, saved networks and
 * keys all live in the NVS partition; photos and maps are files. */
void svc_factory_reset(void)
{
    svc_clear(CLEAR_PHOTOS);
    svc_clear(CLEAR_MAPS);
    nvs_flash_deinit();
    nvs_flash_erase();
    esp_restart();
}

uint32_t svc_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

time_t svc_time(void)
{
    return time(NULL);
}

bool svc_time_valid(void)
{
    return time(NULL) > TIME_VALID_AFTER;
}

void svc_localtime(struct tm *out)
{
    time_t t = time(NULL) + (time_t)CFG.tz_minutes * 60;
    gmtime_r(&t, out);
}

/* ------------------------------------------------------------- key-value */

bool svc_kv_load(const char *key, void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("orbit", NVS_READONLY, &h) != ESP_OK) return false;
    size_t got = len;
    esp_err_t err = nvs_get_blob(h, key, buf, &got);
    nvs_close(h);
    return err == ESP_OK && got == len;
}

void svc_kv_save(const char *key, const void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("orbit", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, key, buf, len);
    nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------- display + power */

void svc_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    bsp_display_brightness_set(percent);
}

void svc_restart(void)
{
    esp_restart();
}

void svc_power_off(void)
{
    uint8_t v = 0;
    if (pmic && reg_read(pmic, 0x10, &v, 1) == ESP_OK) {
        bsp_display_brightness_set(0);
        reg_write(pmic, 0x10, v | 0x01); /* AXP2101 soft power-off */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    /* Still running: no power chip answered, or USB keeps the board up. */
    ESP_LOGW(TAG, "power off not available, restarting instead");
    esp_restart();
}

void svc_memory_info(uint32_t *internal_kb, uint32_t *external_kb)
{
    *internal_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    *external_kb = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

/* -------------------------------------------------------- power chip poll */

static void battery_apply(void *arg)
{
    batt_t *b = arg;
    bool changed = memcmp(&S.batt, b, sizeof(*b)) != 0;
    bool plugged_in = b->plugged && !S.batt.plugged;
    S.batt = *b;
    free(b);
    if (plugged_in) shell_wake();
    if (changed) bus_emit(EVT_BATTERY, NULL);
}

static void battery_read(void)
{
    uint8_t s1 = 0, s2 = 0, pct = 0;
    if (!pmic || reg_read(pmic, 0x00, &s1, 1) != ESP_OK || reg_read(pmic, 0x01, &s2, 1) != ESP_OK) return;
    batt_t *b = malloc(sizeof(*b));
    if (!b) return;
    memset(b, 0, sizeof(*b));
    bool present = s1 & 0x08;
    b->plugged = s1 & 0x20;
    b->charging = ((s2 >> 5) & 0x07) == 0x01;
    b->percent = present && reg_read(pmic, 0xA4, &pct, 1) == ESP_OK && pct <= 100 ? pct : -1;
    ui_post(battery_apply, b);
}

static void button_apply(void *arg)
{
    uintptr_t v = (uintptr_t)arg;
    shell_button((int)(v >> 8), (int)(v & 0xFF));
}

static void button_post(int button, int press)
{
    ui_post(button_apply, (void *)(uintptr_t)((button << 8) | press));
}

static void pmic_init(void)
{
    pmic = i2c_attach(AXP2101_ADDR, "power chip (AXP2101)");
    if (!pmic) return;
    /* Only status and interrupt flags are touched. Voltage rails and charge
     * settings stay exactly as the board ships. */
    reg_write(pmic, 0x40, 0x00);
    reg_write(pmic, 0x41, 0xCC); /* power key short/long, USB in/out */
    reg_write(pmic, 0x42, 0x18); /* charge start/done */
    for (uint8_t r = 0x48; r <= 0x4A; r++) reg_write(pmic, r, 0xFF);
}

static void pmic_poll(void)
{
    uint8_t s[3] = {0};
    if (reg_read(pmic, 0x48, s, 3) != ESP_OK) return;
    if (!(s[0] | s[1] | s[2])) return;
    for (int i = 0; i < 3; i++) {
        if (s[i]) reg_write(pmic, (uint8_t)(0x48 + i), s[i]);
    }
    if (s[1] & 0x04) button_post(BTN_POWER, PRESS_LONG);
    else if (s[1] & 0x08) button_post(BTN_POWER, PRESS_SHORT);
    if ((s[1] & 0xC0) || (s[2] & 0x18)) battery_read();
}

/* ---------------------------------------------------------- motion sensor */

static void imu_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    uint8_t addr = 0;
    if (i2c_master_probe(bus, QMI8658_ADDRESS_HIGH, 50) == ESP_OK) addr = QMI8658_ADDRESS_HIGH;
    else if (i2c_master_probe(bus, QMI8658_ADDRESS_LOW, 50) == ESP_OK) addr = QMI8658_ADDRESS_LOW;
    if (!addr || qmi8658_init(&imu, bus, addr) != ESP_OK) {
        ESP_LOGW(TAG, "motion sensor not found");
        return;
    }
    qmi8658_set_accel_range(&imu, QMI8658_ACCEL_RANGE_4G);
    qmi8658_set_accel_odr(&imu, QMI8658_ACCEL_ODR_62_5HZ);
    qmi8658_enable_sensors(&imu, QMI8658_ENABLE_ACCEL);
    qmi8658_set_accel_unit_mg(&imu, true);
    have_imu = true;
    ESP_LOGI(TAG, "motion sensor found at 0x%02x", addr);
}

/* Latest gravity reading in g, for the Level and Synth apps. Which way x and
 * y point relative to the screen has to be checked on the real watch. */
static volatile float tilt[3] = {0, 0, 1};

void svc_tilt(float *x, float *y, float *z)
{
    *x = tilt[0], *y = tilt[1], *z = tilt[2];
}

static void raise_apply(void *arg)
{
    LV_UNUSED(arg);
    shell_raised();
}

static void steps_apply(void *arg)
{
    S.steps += (uint32_t)(uintptr_t)arg;
}

/* Runs at 25 Hz. Units are milli-g. */
static void imu_poll(void)
{
    static float base = 1000, up_slow, up_fast;
    static bool above;
    static uint32_t last_step_ms, last_raise_ms;
    static int pending_steps, ticks;

    float x, y, z;
    if (qmi8658_read_accel(&imu, &x, &y, &z) != ESP_OK) return; /* in mg, set in imu_init */
    float mag = sqrtf(x * x + y * y + z * z);
    if (mag < 1) return;
    tilt[0] = x / 1000.0f, tilt[1] = y / 1000.0f, tilt[2] = z / 1000.0f;
    uint32_t now = svc_millis();

    /* Steps: count swings of the total force above its running average. */
    base += (mag - base) * 0.05f;
    float swing = mag - base;
    if (!above && swing > 140 && now - last_step_ms > 280) {
        above = true;
        last_step_ms = now;
        pending_steps++;
    } else if (above && swing < 40) {
        above = false;
    }
    if (++ticks % 125 == 0 && pending_steps) {
        ui_post(steps_apply, (void *)(uintptr_t)pending_steps);
        pending_steps = 0;
    }

    /* Raise to wake: the face has just turned upwards. `up` is how much of
     * gravity points out of the screen; compare a quick and a slow average. */
    float up = z / mag;
    up_fast += (up - up_fast) * 0.4f;
    up_slow += (up - up_slow) * 0.04f;
    if (up_fast > 0.55f && up_fast - up_slow > 0.35f && now - last_raise_ms > 1500) {
        last_raise_ms = now;
        ui_post(raise_apply, NULL);
    }
}

/* ------------------------------------------------------------ input task */

static void inputs_task(void *arg)
{
    LV_UNUSED(arg);
    int held_ms = 0, since_tap_ms = -1; /* -1: no tap waiting to be reported */
    bool long_sent = false;
    for (uint32_t n = 1;; n++) {
        vTaskDelay(pdMS_TO_TICKS(20));

        if (gpio_get_level(SIDE_BUTTON) == 0) {
            held_ms += 20;
            if (held_ms >= 600 && !long_sent) {
                long_sent = true;
                since_tap_ms = -1;
                button_post(BTN_SIDE, PRESS_LONG);
            }
        } else {
            if (held_ms >= 40 && !long_sent) {
                /* A second tap soon after the first is a double press; a
                 * single tap is only reported once that chance has passed. */
                if (since_tap_ms >= 0) button_post(BTN_SIDE, PRESS_DOUBLE), since_tap_ms = -1;
                else since_tap_ms = 0;
            }
            held_ms = 0, long_sent = false;
        }
        if (since_tap_ms >= 0 && held_ms == 0 && (since_tap_ms += 20) > 300) {
            button_post(BTN_SIDE, PRESS_SHORT);
            since_tap_ms = -1;
        }
        if (pmic && n % 8 == 0) pmic_poll();
        if (pmic && n % 500 == 0) battery_read();
        if (have_imu && n % 2 == 0) imu_poll();
    }
}

/* ------------------------------------------------------------------- main */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = false};
    esp_pm_configure(&pm);

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    cfg.lv_adapter_cfg.task_stack_size = 14 * 1024; /* the UI draws a lot by hand */
    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "display failed to start");
        return;
    }

    pmic_init();
    rtc = i2c_attach(PCF85063_ADDR, "clock chip (PCF85063)");
    rtc_load();
    imu_init();

    gpio_config_t io = {.pin_bit_mask = 1ULL << SIDE_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);

    bsp_display_lock(-1);
    orbit_boot();
    bsp_display_unlock();

    battery_read();
    /* Stack in external RAM: this task only talks I2C, and internal RAM is scarce. */
    xTaskCreateWithCaps(inputs_task, "inputs", 4096, NULL, 5, NULL, MALLOC_CAP_SPIRAM);

    /* Storage comes after the watch face is up: the very first boot formats
     * the photo partition, which takes a while, and a lit screen is a better
     * thing to look at than a dark one. */
    storage_init();
    photos_changed();

    audio_init();
    net_init();
    web_start();
    ble_init();
    tiles_init();
    lumix_init();
    agent_init();
    /* Got this far, so this firmware works: keep it. A wireless update that
     * crashes before here is rolled back by the bootloader on the next reset. */
    esp_ota_mark_app_valid_cancel_rollback();

    uint32_t internal_kb, external_kb;
    svc_memory_info(&internal_kb, &external_kb);
    ESP_LOGI(TAG, "Orbit %s ready. Free memory: %u KB internal, %u KB external", ORBIT_VERSION, (unsigned)internal_kb,
             (unsigned)external_kb);
}
