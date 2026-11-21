#include "m3_theme.h"
#include "../drivers/display_drv.h"

SystemState g_sys;

static uint32_t s_pulseUntilMs = 0;

void m3ThemeInit() {
    lv_disp_t* disp = lv_disp_get_default();
    lv_theme_t* th = lv_theme_default_init(
        disp,
        lv_color_hex(M3Color::PRIMARY),
        lv_color_hex(M3Color::SECONDARY),
        true,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(disp, th);
    g_sys.lastMinMs = millis();
}

uint32_t m3CurrentBgHex() {
    return g_sys.amoledBg ? M3Color::BG_AMOLED : M3Color::BG_DARK;
}

void m3PulseFeedback() {
    if (!g_sys.ledFeedback || g_sys.flashlightOn) return;
    hwRgbLed(0, 0, 255);
    s_pulseUntilMs = millis() + 28;
}

void m3SyncSysTick() {
    uint32_t now = millis();
    if (s_pulseUntilMs && (int32_t)(now - s_pulseUntilMs) >= 0) {
        s_pulseUntilMs = 0;
        if (!g_sys.flashlightOn) hwRgbLed(0, 0, 0);
    }
    if (now - g_sys.lastMinMs >= 60000UL) {
        g_sys.lastMinMs += 60000UL;
        g_sys.minute++;
        if (g_sys.minute >= 60) {
            g_sys.minute = 0;
            g_sys.hour = (g_sys.hour + 1) % 24;
        }
    }
}
