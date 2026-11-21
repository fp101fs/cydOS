#include "quick_settings.h"
#include "status_bar.h"
#include "app_manager.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_widgets.h"
#include <stdio.h>

static lv_obj_t* s_shade = nullptr;
static bool s_isOpen = false;

static lv_obj_t* s_qsTime = nullptr;
static lv_obj_t* s_qsBatt = nullptr;
static lv_obj_t* s_tileWifi = nullptr;
static lv_obj_t* s_tileWifiSub = nullptr;
static lv_obj_t* s_tileBt = nullptr;
static lv_obj_t* s_tileBtSub = nullptr;
static lv_obj_t* s_tileAir = nullptr;
static lv_obj_t* s_tileAirSub = nullptr;
static lv_obj_t* s_tileTorch = nullptr;
static lv_obj_t* s_tileTorchSub = nullptr;
static lv_obj_t* s_brightSlider = nullptr;
static lv_obj_t* s_brightLbl = nullptr;

static lv_coord_t s_dragStartY = 0;

static void animYCb(void* var, int32_t v) {
    lv_obj_set_y((lv_obj_t*)var, v);
}

static void closeReadyCb(lv_anim_t* a) {
    (void)a;
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
    s_isOpen = false;
}

static void styleTile(lv_obj_t* tile, bool active, uint32_t activeBg) {
    if (active) {
        lv_obj_set_style_bg_color(tile, lv_color_hex(activeBg), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(activeBg), 0);
    } else {
        lv_obj_set_style_bg_color(tile, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(m3OutlineHex()), 0);
    }
}

static lv_obj_t* createQsTile(
    lv_obj_t* parent,
    int16_t x, int16_t y,
    const char* icon,
    const char* title,
    const char* sub,
    lv_obj_t** outSub,
    lv_event_cb_t cb
) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 106, 56);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_pad_hor(btn, 10, 0);
    lv_obj_set_style_pad_ver(btn, 8, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* ico = lv_label_create(btn);
    lv_label_set_text(ico, icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, -6);

    lv_obj_t* tLbl = lv_label_create(btn);
    lv_label_set_text(tLbl, title);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(tLbl, LV_ALIGN_LEFT_MID, 24, -7);

    lv_obj_t* sLbl = lv_label_create(btn);
    lv_label_set_text(sLbl, sub);
    lv_obj_set_style_text_color(sLbl, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_width(sLbl, 84);
    lv_label_set_long_mode(sLbl, LV_LABEL_LONG_DOT);
    lv_obj_align(sLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    *outSub = sLbl;
    return btn;
}

static void wifiTileCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    g_sys.wifiEnabled = !g_sys.wifiEnabled;
    if (g_sys.wifiEnabled && g_sys.airplaneMode) g_sys.airplaneMode = false;
    quickSettingsRefresh();
    statusBarRefresh();
}

static void btTileCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    g_sys.btEnabled = !g_sys.btEnabled;
    if (g_sys.btEnabled && g_sys.airplaneMode) g_sys.airplaneMode = false;
    quickSettingsRefresh();
    statusBarRefresh();
}

static void airTileCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    g_sys.airplaneMode = !g_sys.airplaneMode;
    quickSettingsRefresh();
    statusBarRefresh();
}

static void torchTileCb(lv_event_t* e) {
    (void)e;
    g_sys.flashlightOn = !g_sys.flashlightOn;
    hwRgbLed(g_sys.flashlightOn ? 255 : 0, g_sys.flashlightOn ? 255 : 0, g_sys.flashlightOn ? 255 : 0);
    quickSettingsRefresh();
}

static void brightSliderCb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    displaySetBrightness((uint8_t)v);
    char buf[8];
    snprintf(buf, sizeof(buf), "%ld%%", (long)v);
    lv_label_set_text(s_brightLbl, buf);
}

static void settingsGearCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    quickSettingsClose();
    appManagerLaunch(APP_SETTINGS);
}

static void shadeDragCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        s_dragStartY = pt.y;
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        if (s_dragStartY - pt.y > 26) {
            quickSettingsClose();
        }
    }
}

static void closeBtnCb(lv_event_t* e) {
    (void)e;
    quickSettingsClose();
}

void quickSettingsInit() {
    uint16_t w = displayWidth();
    uint16_t h = displayHeight();

    s_shade = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_shade, w, h);
    lv_obj_set_pos(s_shade, 0, -(int16_t)h);
    lv_obj_set_style_bg_opa(s_shade, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_shade, 0, 0);
    lv_obj_set_style_radius(s_shade, 0, 0);
    lv_obj_set_style_pad_all(s_shade, 0, 0);
    lv_obj_clear_flag(s_shade, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_shade, shadeDragCb, LV_EVENT_ALL, nullptr);

    s_qsTime = lv_label_create(s_shade);
    lv_obj_set_style_text_font(s_qsTime, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_qsTime, 14, 14);

    lv_obj_t* dateLbl = lv_label_create(s_shade);
    lv_label_set_text(dateLbl, "Sat, Nov 21");
    lv_obj_set_style_text_color(dateLbl, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(dateLbl, 14, 38);

    s_qsBatt = lv_label_create(s_shade);
    lv_obj_set_style_text_color(s_qsBatt, lv_color_hex(M3Color::TERTIARY), 0);
    lv_obj_set_style_text_font(s_qsBatt, &lv_font_montserrat_12, 0);
    lv_obj_align(s_qsBatt, LV_ALIGN_TOP_RIGHT, -50, 18);

    lv_obj_t* gearBtn = lv_btn_create(s_shade);
    lv_obj_set_size(gearBtn, 34, 34);
    lv_obj_align(gearBtn, LV_ALIGN_TOP_RIGHT, -10, 12);
    lv_obj_set_style_bg_color(gearBtn, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_radius(gearBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(gearBtn, 0, 0);
    lv_obj_add_event_cb(gearBtn, settingsGearCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gearIco = lv_label_create(gearBtn);
    lv_label_set_text(gearIco, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gearIco, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_center(gearIco);

    s_tileWifi = createQsTile(s_shade, 10, 68, LV_SYMBOL_WIFI, "Wi-Fi", g_sys.wifiSsid, &s_tileWifiSub, wifiTileCb);
    s_tileBt = createQsTile(s_shade, 124, 68, LV_SYMBOL_BLUETOOTH, "Bluetooth", g_sys.btDevice, &s_tileBtSub, btTileCb);
    s_tileAir = createQsTile(s_shade, 10, 132, LV_SYMBOL_GPS, "Airplane", "Off", &s_tileAirSub, airTileCb);
    s_tileTorch = createQsTile(s_shade, 124, 132, LV_SYMBOL_EYE_OPEN, "Torch", "Off", &s_tileTorchSub, torchTileCb);

    lv_obj_t* brCard = m3Card(s_shade, 220, 64, 18);
    lv_obj_set_pos(brCard, 10, 200);
    lv_obj_set_style_pad_hor(brCard, 12, 0);

    lv_obj_t* brTitle = lv_label_create(brCard);
    lv_label_set_text(brTitle, "Display Brightness");
    lv_obj_set_style_text_color(brTitle, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(brTitle, &lv_font_montserrat_10, 0);
    lv_obj_align(brTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_brightLbl = lv_label_create(brCard);
    lv_obj_set_style_text_color(s_brightLbl, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(s_brightLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_brightLbl, LV_ALIGN_TOP_RIGHT, 0, -2);

    s_brightSlider = lv_slider_create(brCard);
    lv_obj_set_size(s_brightSlider, 192, 16);
    lv_obj_align(s_brightSlider, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(s_brightSlider, 10, 100);
    lv_slider_set_value(s_brightSlider, displayGetBrightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_brightSlider, lv_color_hex(M3Color::SURFACE_HIGHEST), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_brightSlider, lv_color_hex(M3Color::PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_brightSlider, lv_color_hex(M3Color::TEXT_PRIMARY), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_brightSlider, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(s_brightSlider, brightSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* closeBtn = lv_btn_create(s_shade);
    lv_obj_set_size(closeBtn, 96, 26);
    lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_radius(closeBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_add_event_cb(closeBtn, closeBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* upIco = lv_label_create(closeBtn);
    lv_label_set_text(upIco, LV_SYMBOL_UP "  Close");
    lv_obj_set_style_text_color(upIco, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(upIco, &lv_font_montserrat_10, 0);
    lv_obj_center(upIco);

    quickSettingsRefreshTheme();
    quickSettingsRefresh();
}

void quickSettingsRefreshTheme() {
    if (!s_shade) return;
    lv_obj_set_style_bg_color(s_shade, lv_color_hex(g_sys.lightMode ? 0xEAEFF7 : 0x11131B), 0);
    lv_obj_set_style_text_color(s_qsTime, lv_color_hex(m3TextPrimaryHex()), 0);
    quickSettingsRefresh();
}

void quickSettingsRefresh() {
    if (!s_shade) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u", g_sys.hour, g_sys.minute);
    lv_label_set_text(s_qsTime, buf);

    snprintf(buf, sizeof(buf), "%u%%", g_sys.batteryPct);
    lv_label_set_text(s_qsBatt, buf);

    snprintf(buf, sizeof(buf), "%u%%", displayGetBrightness());
    lv_label_set_text(s_brightLbl, buf);

    bool wifiActive = g_sys.wifiEnabled && !g_sys.airplaneMode;
    styleTile(s_tileWifi, wifiActive, M3Color::PRIMARY_CONT);
    lv_label_set_text(s_tileWifiSub, wifiActive ? g_sys.wifiSsid : "Off");

    bool btActive = g_sys.btEnabled && !g_sys.airplaneMode;
    styleTile(s_tileBt, btActive, M3Color::SECONDARY_CONT);
    lv_label_set_text(s_tileBtSub, btActive ? g_sys.btDevice : "Off");

    styleTile(s_tileAir, g_sys.airplaneMode, M3Color::CYAN_CONT);
    lv_label_set_text(s_tileAirSub, g_sys.airplaneMode ? "Active" : "Off");

    styleTile(s_tileTorch, g_sys.flashlightOn, M3Color::TERTIARY_CONT);
    lv_label_set_text(s_tileTorchSub, g_sys.flashlightOn ? "On" : "Off");
}

void quickSettingsOpen() {
    if (!s_shade || s_isOpen) return;
    s_isOpen = true;
    quickSettingsRefresh();
    lv_obj_clear_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_shade);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_shade);
    lv_anim_set_values(&a, lv_obj_get_y(s_shade), 0);
    lv_anim_set_time(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_start(&a);
}

void quickSettingsClose() {
    if (!s_shade || !s_isOpen) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_shade);
    lv_anim_set_values(&a, lv_obj_get_y(s_shade), -(int32_t)displayHeight());
    lv_anim_set_time(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_set_ready_cb(&a, closeReadyCb);
    lv_anim_start(&a);
}

bool quickSettingsIsOpen() {
    return s_isOpen;
}
