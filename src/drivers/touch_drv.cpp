#include "touch_drv.h"
#include "display_drv.h"
#include "../config/hw_config.h"
#include <Preferences.h>

// ST7789 dual-USB CYD has XPT2046 X-axis mirrored relative to rotation(0)
#ifdef ST7789_2_DRIVER
static int16_t s_calX0 = 3850, s_calX1 = 220;
#else
static int16_t s_calX0 = 220, s_calX1 = 3850;
#endif
static int16_t s_calY0 = 220, s_calY1 = 3750;
static bool s_calLoaded = false;
static lv_indev_drv_t s_indev_drv;

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
    z = xptTransfer(0xB0);
    if (z < TOUCH_Z_MIN) return false;
    x = xptTransfer(0xD0);
    y = xptTransfer(0x90);
    return true;
}

bool touchReadPoint(TouchPoint& out) {
    out.pressed = false;
    uint16_t rx1, ry1, rz1;
    if (!xptSample(rx1, ry1, rz1)) return false;

    uint16_t rx2, ry2, rz2;
    if (!xptSample(rx2, ry2, rz2)) return false;

    uint16_t rx = (rx1 + rx2) / 2;
    uint16_t ry = (ry1 + ry2) / 2;

    int16_t w = (int16_t)displayWidth();
    int16_t h = (int16_t)displayHeight();

    int16_t x = (int16_t)map((long)rx, s_calX0, s_calX1, 0, w - 1);
    int16_t y = (int16_t)map((long)ry, s_calY0, s_calY1, 0, h - 1);
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;

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
            wasPressed = true;
            Serial.printf("[TOUCH] PR x=%d y=%d\n", lastX, lastY);
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
        if (wasPressed) {
            wasPressed = false;
            Serial.println("[TOUCH] REL");
        }
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
    if (prefs.begin("touch_p8", true)) {
        if (prefs.getBool("ok", false)) {
            s_calX0 = prefs.getShort("x0", s_calX0);
            s_calX1 = prefs.getShort("x1", s_calX1);
            s_calY0 = prefs.getShort("y0", s_calY0);
            s_calY1 = prefs.getShort("y1", s_calY1);
            s_calLoaded = true;
        }
        prefs.end();
    }

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = lvglTouchReadCb;
    lv_indev_drv_register(&s_indev_drv);
}

bool touchIsCalibrated() { return s_calLoaded; }

static void waitTap(uint16_t& x, uint16_t& y) {
    while (true) {
        uint16_t rx, ry, rz;
        if (xptSample(rx, ry, rz)) {
            delay(30);
            uint32_t ax = 0, ay = 0;
            uint16_t n = 0;
            for (int i = 0; i < 8; ++i) {
                if (xptSample(rx, ry, rz)) {
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

    int32_t dx = (int32_t)r1x - (int32_t)r0x;
    int32_t dy = (int32_t)r1y - (int32_t)r0y;
    s_calX0 = (int16_t)(r0x - (dx * 20) / (w - 41));
    s_calX1 = (int16_t)(r1x + (dx * 20) / (w - 41));
    s_calY0 = (int16_t)(r0y - (dy * 20) / (h - 41));
    s_calY1 = (int16_t)(r1y + (dy * 20) / (h - 41));
    if (s_calX0 == s_calX1) s_calX1 = s_calX0 + 1;
    if (s_calY0 == s_calY1) s_calY1 = s_calY0 + 1;

    Preferences prefs;
    if (prefs.begin("touch_p8", false)) {
        prefs.putBool("ok", true);
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
