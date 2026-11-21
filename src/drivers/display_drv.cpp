#include "display_drv.h"
#include "../config/hw_config.h"
#include <WiFi.h>
#include <esp_sleep.h>

TFT_eSPI tft;

// 24-line draw buffer in internal DRAM: 240 * 24 * 2 = 11,520 bytes
static constexpr uint16_t DRAW_BUF_LINES = 24;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[DEFAULT_SCREEN_W * DRAW_BUF_LINES];
static lv_disp_drv_t s_disp_drv;

static uint16_t s_width = DEFAULT_SCREEN_W;
static uint16_t s_height = DEFAULT_SCREEN_H;
static uint8_t s_brightness = 85;

static void IRAM_ATTR dispFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    // LV_COLOR_16_SWAP == 1 in lv_conf.h, so bytes are already big-endian for SPI
    tft.pushColors((uint16_t*)&color_p->full, w * h, false);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

void displayInit() {
    pinMode(PIN_BOOT, INPUT_PULLUP);
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    hwRgbLed(0, 0, 0);

    // Initialize PWM backlight on both possible CYD backlight pins
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RES);
    ledcAttachPin(CYD_BL_PIN, BL_PWM_CHANNEL);
    if (CYD_BL_PIN_ALT != CYD_BL_PIN) {
        ledcAttachPin(CYD_BL_PIN_ALT, BL_PWM_CHANNEL);
    }
    displaySetBrightness(s_brightness);

    tft.init();
    tft.setRotation(0);  // Portrait orientation (240x320)
    tft.fillScreen(TFT_BLACK);

    s_width = (uint16_t)tft.width();
    s_height = (uint16_t)tft.height();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, s_width * DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = s_width;
    s_disp_drv.ver_res = s_height;
    s_disp_drv.flush_cb = dispFlushCb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);
}

uint16_t displayWidth() { return s_width; }
uint16_t displayHeight() { return s_height; }

void displaySetBrightness(uint8_t percent) {
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    s_brightness = percent;
    // Gamma-like curve so low slider values feel natural
    uint32_t duty = ((uint32_t)percent * (uint32_t)percent * 255U) / 10000U;
    if (duty < 12) duty = 12;
    ledcWrite(BL_PWM_CHANNEL, duty);
}

uint8_t displayGetBrightness() { return s_brightness; }

void hwRgbLed(uint8_t r, uint8_t g, uint8_t b) {
    digitalWrite(PIN_LED_R, r ? LOW : HIGH);
    digitalWrite(PIN_LED_G, g ? LOW : HIGH);
    digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

bool hwBootPressed() {
    return digitalRead(PIN_BOOT) == LOW;
}

void hwReboot() {
    Serial.println("[PWR] Rebooting...");
    Serial.flush();
    hwRgbLed(0, 0, 0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(80);
    ESP.restart();
}

void hwShutdown() {
    Serial.println("[PWR] Entering deep sleep (press BOOT or RST to wake)");
    Serial.flush();
    hwRgbLed(0, 0, 0);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Turn off backlight completely
    ledcWrite(BL_PWM_CHANNEL, 0);
    ledcDetachPin(CYD_BL_PIN);
    pinMode(CYD_BL_PIN, OUTPUT);
    digitalWrite(CYD_BL_PIN, LOW);
    if (CYD_BL_PIN_ALT != CYD_BL_PIN) {
        ledcDetachPin(CYD_BL_PIN_ALT);
        pinMode(CYD_BL_PIN_ALT, OUTPUT);
        digitalWrite(CYD_BL_PIN_ALT, LOW);
    }

    // Put TFT controller into sleep mode
    tft.writecommand(0x28); // Display OFF
    delay(10);
    tft.writecommand(0x10); // Sleep IN
    delay(120);

    // Wait for BOOT release if currently held so ext0 doesn't immediately re-trigger
    while (hwBootPressed()) {
        delay(20);
    }
    delay(40);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOOT, 0);
    esp_deep_sleep_start();
}
