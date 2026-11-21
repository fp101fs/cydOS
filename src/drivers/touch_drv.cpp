#include "touch_drv.h"
#include "display_drv.h"
#include "../config/hw_config.h"
#include "../theme/m3_theme.h"
#include <Preferences.h>

static int16_t s_calX0 = 200, s_calX1 = 3900;
static int16_t s_calY0 = 200, s_calY1 = 3700;
static bool s_calLoaded = false;
static uint8_t s_sensitivity = TOUCH_SENS_DEFAULT;
static uint16_t s_zMin = TOUCH_Z_MIN;
static int16_t s_maxDelta = TOUCH_SAMPLE_MAX_DELTA;
static lv_indev_drv_t s_indev_drv;

// ============================================================================
// Tap Pulse Ripple Overlay (2-slot pool on lv_layer_sys, zero per-tap alloc)
// ============================================================================

static constexpr uint8_t  TAP_POOL_SIZE = 2;
static constexpr int16_t  TAP_D_START   = 8;
static constexpr int16_t  TAP_D_END     = 38;
static constexpr uint16_t TAP_ANIM_MS   = 230;

struct TapRipple {
    lv_obj_t* obj;
    int16_t   cx;
    int16_t   cy;
};

static TapRipple s_tapPool[TAP_POOL_SIZE] = {};
static uint8_t   s_tapNextIdx = 0;

static void tapRippleExecCb(void* var, int32_t p) {
    TapRipple* r = (TapRipple*)var;
    if (!r || !r->obj) return;

    int16_t d = (int16_t)(TAP_D_START + ((int32_t)(TAP_D_END - TAP_D_START) * p) / 100);
    lv_obj_set_size(r->obj, d, d);
    lv_obj_set_pos(r->obj, r->cx - d / 2, r->cy - d / 2);

    // Smooth fade out for both crisp ring border and subtle inner glow
    lv_opa_t borderOpa = (lv_opa_t)(((100 - p) * 230) / 100);
    lv_opa_t bgOpa     = (lv_opa_t)(((100 - p) * 72) / 100);
    lv_obj_set_style_border_opa(r->obj, borderOpa, 0);
    lv_obj_set_style_bg_opa(r->obj, bgOpa, 0);
}

static void tapRippleReadyCb(lv_anim_t* a) {
    TapRipple* r = (TapRipple*)a->var;
    if (r && r->obj) {
        lv_obj_add_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void tapRippleInit() {
    for (uint8_t i = 0; i < TAP_POOL_SIZE; ++i) {
        lv_obj_t* ring = lv_obj_create(lv_layer_sys());
        lv_obj_set_size(ring, TAP_D_START, TAP_D_START);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_shadow_width(ring, 0, 0);
        lv_obj_set_style_pad_all(ring, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        s_tapPool[i].obj = ring;
    }
}

static void tapRippleAsyncCb(void* userData) {
    uint32_t packed = (uint32_t)(uintptr_t)userData;
    int16_t cx = (int16_t)(packed & 0xFFFFU);
    int16_t cy = (int16_t)((packed >> 16) & 0xFFFFU);

    TapRipple* r = &s_tapPool[s_tapNextIdx];
    s_tapNextIdx = (uint8_t)((s_tapNextIdx + 1) % TAP_POOL_SIZE);
    if (!r->obj) return;

    lv_anim_del(r, tapRippleExecCb);
    r->cx = cx;
    r->cy = cy;

    lv_color_t col = lv_color_hex(m3PrimaryHex());
    lv_obj_set_style_border_color(r->obj, col, 0);
    lv_obj_set_style_bg_color(r->obj, col, 0);
    lv_obj_clear_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(r->obj);
    tapRippleExecCb(r, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, TAP_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, tapRippleExecCb);
    lv_anim_set_ready_cb(&a, tapRippleReadyCb);
    lv_anim_start(&a);
}

static uint16_t sensToZMin(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t span = (uint32_t)(TOUCH_Z_MIN_CEIL - TOUCH_Z_MIN_FLOOR);
    return (uint16_t)(TOUCH_Z_MIN_CEIL - ((uint32_t)percent * span) / 100U);
}

static int16_t sensToMaxDelta(uint8_t percent) {
    if (percent > 100) percent = 100;
    int32_t span = (int32_t)(TOUCH_SAMPLE_MAX_DELTA_MAX - TOUCH_SAMPLE_MAX_DELTA);
    return (int16_t)(TOUCH_SAMPLE_MAX_DELTA + ((int32_t)percent * span) / 100);
}

static inline void tPinOut(int pin, bool v) { digitalWrite(pin, v ? HIGH : LOW); }
static inline int tPinIn(int pin) { return digitalRead(pin); }
static inline void tDelay() { delayMicroseconds(2); }

static uint16_t xptTransfer(uint8_t cmd) {
    digitalWrite(PIN_TOUCH_CS, LOW);
    tDelay();
    tPinOut(PIN_TOUCH_CLK, false);

    for (int i = 7; i >= 0; --i) {
        tPinOut(PIN_TOUCH_MOSI, (cmd >> i) & 1);
        tPinOut(PIN_TOUCH_CLK, true);
        tDelay();
        tPinOut(PIN_TOUCH_CLK, false);
        tDelay();
    }

    uint16_t raw = 0;
    for (int i = 0; i < 16; ++i) {
        tPinOut(PIN_TOUCH_CLK, true);
        tDelay();
        raw = (uint16_t)((raw << 1) | (tPinIn(PIN_TOUCH_MISO) ? 1 : 0));
        tPinOut(PIN_TOUCH_CLK, false);
        tDelay();
    }

    digitalWrite(PIN_TOUCH_CS, HIGH);
    return (raw >> 3) & 0x0FFF;
}

static bool xptSample(uint16_t& x, uint16_t& y, uint16_t& z) {
    // At ultra-low Z thresholds (< 15), gate on hardware PENIRQ to prevent ghost taps
    if (s_zMin < 15 && tPinIn(PIN_TOUCH_IRQ) != LOW) return false;
    z = xptTransfer(0xB0);
    if (z < s_zMin) return false;
    x = xptTransfer(0xD0);
    y = xptTransfer(0x90);
    return true;
}

static bool xptSampleFiltered(uint16_t& rx, uint16_t& ry) {
    uint16_t rx1, ry1, rz1;
    if (!xptSample(rx1, ry1, rz1)) return false;

    uint16_t rx2, ry2, rz2;
    if (!xptSample(rx2, ry2, rz2)) return false;

    int16_t dx = (int16_t)rx1 - (int16_t)rx2;
    int16_t dy = (int16_t)ry1 - (int16_t)ry2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx > s_maxDelta || dy > s_maxDelta) return false;

    rx = (uint16_t)((rx1 + rx2) / 2);
    ry = (uint16_t)((ry1 + ry2) / 2);
    return true;
}

bool touchReadPoint(TouchPoint& out) {
    out.pressed = false;
    uint16_t rx, ry;
    if (!xptSampleFiltered(rx, ry)) return false;

    int16_t w = (int16_t)displayWidth();
    int16_t h = (int16_t)displayHeight();

    int16_t x = (int16_t)map((long)rx, s_calX0, s_calX1, 0, w - 1);
    int16_t y = (int16_t)map((long)ry, s_calY0, s_calY1, 0, h - 1);
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;

    // Fix horizontal X mirroring observed on ST7789 portrait CYD
    x = (w - 1) - x;

    out.pressed = true;
    out.x = x;
    out.y = y;
    return true;
}

static void lvglTouchReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    static int16_t lastX = 0;
    static int16_t lastY = 0;
    static bool wasPressed = false;

    TouchPoint pt;
    if (touchReadPoint(pt)) {
        lastX = pt.x;
        lastY = pt.y;
        data->state = LV_INDEV_STATE_PR;
        if (!wasPressed) {
            uint32_t packed = ((uint32_t)(uint16_t)pt.y << 16) | (uint32_t)(uint16_t)pt.x;
            lv_async_call(tapRippleAsyncCb, (void*)(uintptr_t)packed);
        }
        wasPressed = true;
    } else {
        data->state = LV_INDEV_STATE_REL;
        wasPressed = false;
    }
    data->point.x = lastX;
    data->point.y = lastY;
}

void touchInit() {
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    pinMode(PIN_TOUCH_CLK, OUTPUT);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);

    Preferences prefs;
    if (prefs.begin("touch", true)) {
        if (prefs.getBool("okp", false)) {
            s_calX0 = prefs.getShort("x0", s_calX0);
            s_calX1 = prefs.getShort("x1", s_calX1);
            s_calY0 = prefs.getShort("y0", s_calY0);
            s_calY1 = prefs.getShort("y1", s_calY1);
            s_calLoaded = true;
        }
        s_sensitivity = prefs.getUChar("sens", TOUCH_SENS_DEFAULT);
        if (s_sensitivity > 100) s_sensitivity = TOUCH_SENS_DEFAULT;
        prefs.end();
    }
    s_zMin = sensToZMin(s_sensitivity);
    s_maxDelta = sensToMaxDelta(s_sensitivity);
    Serial.printf("[TOUCH] cal loaded=%d x0=%d x1=%d y0=%d y1=%d SENS=%u%% Z_MIN=%u MAX_DELTA=%d\n",
                  s_calLoaded ? 1 : 0, s_calX0, s_calX1, s_calY0, s_calY1,
                  (unsigned)s_sensitivity, (unsigned)s_zMin, (int)s_maxDelta);

    tapRippleInit();

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvglTouchReadCb;
    s_indev_drv.scroll_limit = 20;
    lv_indev_drv_register(&s_indev_drv);
}

bool touchIsCalibrated() { return s_calLoaded; }

void touchSetSensitivity(uint8_t percent, bool saveNvs) {
    if (percent > 100) percent = 100;
    s_sensitivity = percent;
    s_zMin = sensToZMin(percent);
    s_maxDelta = sensToMaxDelta(percent);
    if (saveNvs) {
        Preferences prefs;
        if (prefs.begin("touch", false)) {
            prefs.putUChar("sens", s_sensitivity);
            prefs.end();
        }
        Serial.printf("[TOUCH] sensitivity saved=%u%% (Z_MIN=%u, MAX_DELTA=%d)\n",
                      (unsigned)s_sensitivity, (unsigned)s_zMin, (int)s_maxDelta);
    }
}

uint8_t touchGetSensitivity() { return s_sensitivity; }
uint16_t touchGetZThreshold() { return s_zMin; }

static void waitTap(uint16_t& x, uint16_t& y) {
    while (true) {
        uint16_t rx, ry, rz;
        if (xptSampleFiltered(rx, ry)) {
            delay(30);
            uint32_t ax = 0, ay = 0;
            uint16_t n = 0;
            for (int i = 0; i < 8; ++i) {
                if (xptSampleFiltered(rx, ry)) {
                    ax += rx;
                    ay += ry;
                    n++;
                }
                delay(8);
            }
            if (n >= 4) {
                x = (uint16_t)(ax / n);
                y = (uint16_t)(ay / n);
                while (xptSample(rx, ry, rz)) delay(10);
                delay(200);
                return;
            }
        }
        delay(10);
    }
}

void touchCalibrateInteractive() {
    int16_t w = (int16_t)displayWidth();
    int16_t h = (int16_t)displayHeight();

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Touch Calibration", 18, 24);
    tft.setTextSize(1);
    tft.drawString("Tap each target circle", 48, 52);

    auto drawTarget = [](int sx, int sy, uint16_t col) {
        tft.fillCircle(sx, sy, 14, col);
        tft.drawCircle(sx, sy, 18, TFT_WHITE);
    };

    uint16_t r0x, r0y, r1x, r1y;
    drawTarget(20, 20, TFT_RED);
    waitTap(r0x, r0y);
    tft.fillCircle(20, 20, 18, TFT_GREEN);

    drawTarget(w - 21, h - 21, TFT_RED);
    waitTap(r1x, r1y);
    tft.fillCircle(w - 21, h - 21, 18, TFT_GREEN);

    // Note: touchReadPoint mirrors X, so store raw endpoints for (w-1-x) mapping
    int32_t dx = (int32_t)r1x - (int32_t)r0x;
    int32_t dy = (int32_t)r1y - (int32_t)r0y;
    s_calX1 = (int16_t)(r0x - (dx * 20) / (w - 41));
    s_calX0 = (int16_t)(r1x + (dx * 20) / (w - 41));
    s_calY0 = (int16_t)(r0y - (dy * 20) / (h - 41));
    s_calY1 = (int16_t)(r1y + (dy * 20) / (h - 41));
    if (s_calX0 == s_calX1) s_calX1 = s_calX0 + 1;
    if (s_calY0 == s_calY1) s_calY1 = s_calY0 + 1;

    Preferences prefs;
    if (prefs.begin("touch", false)) {
        prefs.putBool("okp", true);
        prefs.putShort("x0", s_calX0);
        prefs.putShort("x1", s_calX1);
        prefs.putShort("y0", s_calY0);
        prefs.putShort("y1", s_calY1);
        prefs.end();
    }
    s_calLoaded = true;
    tft.fillScreen(TFT_BLACK);
    delay(150);
}
