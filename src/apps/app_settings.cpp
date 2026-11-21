#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../ui/status_bar.h"
#include "../ui/quick_settings.h"
#include "../drivers/touch_drv.h"
#include "../drivers/wifi_mgr.h"

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
    g_sys.btEnabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (g_sys.btEnabled) g_sys.airplaneMode = false;
    statusBarRefresh();
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

static void calBtnCb(lv_event_t* e) {
    (void)e;
    touchCalibrateInteractive();
    lv_obj_invalidate(lv_scr_act());
    m3ShowToast("Touch calibrated!", 1400);
}

void buildAppSettings(lv_obj_t* body) {
    setFlexCol(body);

    m3ToggleRow(body, LV_SYMBOL_WIFI, "Wi-Fi", wifiMgrStatusText(), g_sys.wifiEnabled && !g_sys.airplaneMode, M3Color::PRIMARY, wifiToggleCb);
    m3ToggleRow(body, LV_SYMBOL_BLUETOOTH, "Bluetooth", g_sys.btDevice, g_sys.btEnabled, M3Color::SECONDARY, btToggleCb);
    m3ToggleRow(body, LV_SYMBOL_EYE_OPEN, "AMOLED Black", "Pure #000000 background", g_sys.amoledBg, M3Color::CYAN, amoledToggleCb);
    m3ToggleRow(body, LV_SYMBOL_BELL, "LED Haptics", "Subtle tap pulse", g_sys.ledFeedback, M3Color::TERTIARY, ledToggleCb);

    lv_obj_t* calBtn = m3PillBtn(body, "Recalibrate Touch", M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 38);
    lv_obj_add_event_cb(calBtn, calBtnCb, LV_EVENT_CLICKED, nullptr);
}

REGISTER_APP(settings, "settings", "Settings", LV_SYMBOL_SETTINGS, 0x8AB4F8, 0x1D3557, buildAppSettings, nullptr, 120);
