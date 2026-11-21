#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../ui/status_bar.h"
#include "../ui/quick_settings.h"
#include "../drivers/display_drv.h"
#include "../drivers/touch_drv.h"
#include "../drivers/wifi_mgr.h"
#include "../drivers/ble_mgr.h"
#include <stdio.h>

static lv_obj_t* s_sensLbl = nullptr;
static uint32_t s_rebootArmMs = 0;
static uint32_t s_shutdownArmMs = 0;

static void settingsDeleteCb(lv_event_t* e) {
    (void)e;
    s_sensLbl = nullptr;
    s_rebootArmMs = 0;
    s_shutdownArmMs = 0;
}

static void setFlexCol(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);
}

static void wifiToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    wifiMgrSetEnabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
}

static void btToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bleMgrSetEnabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
}

static void amoledToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_sys.amoledBg = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3ApplyThemeAll();
    m3ShowToast(g_sys.amoledBg ? "AMOLED Black enabled" : "Charcoal theme enabled", 1100);
}

static void ledToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_sys.ledFeedback = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

static void sensSliderCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* sl = lv_event_get_target(e);
    uint8_t val = (uint8_t)lv_slider_get_value(sl);

    if (code == LV_EVENT_VALUE_CHANGED) {
        touchSetSensitivity(val, false);
        if (s_sensLbl) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u%%", (unsigned)val);
            lv_label_set_text(s_sensLbl, buf);
        }
    } else if (code == LV_EVENT_RELEASED) {
        touchSetSensitivity(val, true);
    }
}

static void calBtnCb(lv_event_t* e) {
    (void)e;
    touchCalibrateInteractive();
    lv_obj_invalidate(lv_scr_act());
    m3ShowToast("Touch calibrated!", 1400);
}

static void rebootBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    uint32_t now = millis();
    if (s_rebootArmMs != 0 && (now - s_rebootArmMs) < 3000) {
        s_rebootArmMs = 0;
        m3ShowToast("Rebooting...", 600);
        lv_timer_handler();
        hwReboot();
        return;
    }
    s_rebootArmMs = now;
    s_shutdownArmMs = 0;
    m3ShowToast("Tap Reboot again to restart", 2500);
}

static void shutdownBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    uint32_t now = millis();
    if (s_shutdownArmMs != 0 && (now - s_shutdownArmMs) < 3000) {
        s_shutdownArmMs = 0;
        m3ShowToast("Shutting down...", 600);
        lv_timer_handler();
        hwShutdown();
        return;
    }
    s_shutdownArmMs = now;
    s_rebootArmMs = 0;
    m3ShowToast("Tap Shutdown again (BOOT to wake)", 2500);
}

void buildAppSettings(lv_obj_t* body) {
    setFlexCol(body);
    s_sensLbl = nullptr;
    s_rebootArmMs = 0;
    s_shutdownArmMs = 0;
    lv_obj_add_event_cb(body, settingsDeleteCb, LV_EVENT_DELETE, nullptr);

    m3ToggleRow(body, LV_SYMBOL_WIFI, "Wi-Fi", wifiMgrStatusText(), g_sys.wifiEnabled && !g_sys.airplaneMode, M3Color::PRIMARY, wifiToggleCb);
    m3ToggleRow(body, LV_SYMBOL_BLUETOOTH, "Bluetooth", bleMgrStatusText(), g_sys.btEnabled && !g_sys.airplaneMode, M3Color::SECONDARY, btToggleCb);
    m3ToggleRow(body, LV_SYMBOL_EYE_OPEN, "AMOLED Black", "Pure #000000 background", g_sys.amoledBg, M3Color::CYAN, amoledToggleCb);
    m3ToggleRow(body, LV_SYMBOL_BELL, "LED Haptics", "Subtle tap pulse", g_sys.ledFeedback, M3Color::TERTIARY, ledToggleCb);

    // Touch Sensitivity Slider Card
    lv_obj_t* sensCard = m3Card(body, LV_PCT(100), 64, 16);
    lv_obj_set_style_pad_hor(sensCard, 12, 0);
    lv_obj_set_style_pad_ver(sensCard, 8, 0);

    lv_obj_t* sensTitle = lv_label_create(sensCard);
    lv_label_set_text(sensTitle, "Touch Sensitivity");
    lv_obj_set_style_text_color(sensTitle, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(sensTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(sensTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_sensLbl = lv_label_create(sensCard);
    char sensBuf[16];
    snprintf(sensBuf, sizeof(sensBuf), "%u%%", (unsigned)touchGetSensitivity());
    lv_label_set_text(s_sensLbl, sensBuf);
    lv_obj_set_style_text_color(s_sensLbl, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(s_sensLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_sensLbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t* sensSlider = lv_slider_create(sensCard);
    lv_obj_set_size(sensSlider, LV_PCT(100), 14);
    lv_obj_align(sensSlider, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(sensSlider, 0, 100);
    lv_slider_set_value(sensSlider, touchGetSensitivity(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sensSlider, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sensSlider, lv_color_hex(m3PrimaryHex()), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sensSlider, lv_color_hex(m3TextPrimaryHex()), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sensSlider, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(sensSlider, sensSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sensSlider, sensSliderCb, LV_EVENT_RELEASED, nullptr);

    lv_obj_t* calBtn = m3PillBtn(body, "Recalibrate Touch", M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 36);
    lv_obj_add_event_cb(calBtn, calBtnCb, LV_EVENT_CLICKED, nullptr);

    // Power Row: Reboot | Shutdown
    lv_obj_t* pwrRow = lv_obj_create(body);
    lv_obj_set_size(pwrRow, LV_PCT(100), 38);
    lv_obj_set_style_bg_opa(pwrRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pwrRow, 0, 0);
    lv_obj_set_style_pad_all(pwrRow, 0, 0);
    lv_obj_clear_flag(pwrRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(pwrRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pwrRow, 8, 0);

    lv_obj_t* rebootBtn = m3PillBtn(pwrRow, LV_SYMBOL_REFRESH " Reboot", 0x3D3214, M3Color::AMBER, 0, 36);
    lv_obj_set_flex_grow(rebootBtn, 1);
    lv_obj_add_event_cb(rebootBtn, rebootBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* shutdownBtn = m3PillBtn(pwrRow, LV_SYMBOL_POWER " Shutdown", 0x4A1D1D, M3Color::CORAL, 0, 36);
    lv_obj_set_flex_grow(shutdownBtn, 1);
    lv_obj_add_event_cb(shutdownBtn, shutdownBtnCb, LV_EVENT_CLICKED, nullptr);
}

REGISTER_APP(settings, "settings", "Settings", LV_SYMBOL_SETTINGS, 0x8AB4F8, 0x475569, buildAppSettings, nullptr, 120);
