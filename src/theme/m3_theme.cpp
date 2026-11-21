#include "m3_theme.h"
#include "../drivers/display_drv.h"
#include "../ui/home_screen.h"
#include "../ui/status_bar.h"
#include "../ui/nav_bar.h"
#include "../ui/quick_settings.h"
#include "../ui/app_manager.h"
#include "../components/m3_keyboard.h"

SystemState g_sys;

static uint32_t s_pulseUntilMs = 0;

void m3ThemeInit() {
    lv_disp_t* disp = lv_disp_get_default();
    lv_theme_t* th = lv_theme_default_init(
        disp,
        lv_color_hex(m3PrimaryHex()),
        lv_color_hex(M3Color::SECONDARY),
        !g_sys.lightMode,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(disp, th);
}

uint32_t m3CurrentBgHex() {
    if (g_sys.lightMode) return 0xF2F4F8;
    return g_sys.amoledBg ? M3Color::BG_AMOLED : M3Color::BG_DARK;
}

uint32_t m3SurfaceHex() {
    return g_sys.lightMode ? 0xFFFFFF : M3Color::SURFACE;
}

uint32_t m3SurfaceHighHex() {
    return g_sys.lightMode ? 0xE6EBF3 : M3Color::SURFACE_HIGH;
}

uint32_t m3SurfaceHighestHex() {
    return g_sys.lightMode ? 0xD7DFEC : M3Color::SURFACE_HIGHEST;
}

uint32_t m3OutlineHex() {
    return g_sys.lightMode ? 0xC2CBD9 : M3Color::OUTLINE;
}

uint32_t m3TextPrimaryHex() {
    return g_sys.lightMode ? 0x1A1C20 : M3Color::TEXT_PRIMARY;
}

uint32_t m3TextSecondaryHex() {
    return g_sys.lightMode ? 0x5B616E : M3Color::TEXT_SECONDARY;
}

uint32_t m3PrimaryHex() {
    return g_sys.lightMode ? 0x1A73E8 : M3Color::PRIMARY;
}

void m3ApplyThemeAll() {
    homeScreenRefreshTheme();
    statusBarRefreshTheme();
    navBarRefreshTheme();
    quickSettingsRefreshTheme();
    m3KeyboardRefreshTheme();
    appManagerRefreshTheme();
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
}
