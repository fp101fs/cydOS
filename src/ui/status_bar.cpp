#include "status_bar.h"
#include "quick_settings.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include <stdio.h>

static lv_obj_t* s_bar = nullptr;
static lv_obj_t* s_timeLbl = nullptr;
static lv_obj_t* s_btLbl = nullptr;
static lv_obj_t* s_wifiLbl = nullptr;
static lv_obj_t* s_airLbl = nullptr;
static lv_obj_t* s_cellBars[4] = {nullptr};
static lv_obj_t* s_battBody = nullptr;
static lv_obj_t* s_battNub = nullptr;
static lv_obj_t* s_battFill = nullptr;
static lv_obj_t* s_battLbl = nullptr;

static lv_coord_t s_pressY = 0;

static void barEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        s_pressY = pt.y;
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        if (pt.y - s_pressY > 14) {
            quickSettingsOpen();
        }
    } else if (code == LV_EVENT_CLICKED) {
        quickSettingsOpen();
    }
}

void statusBarInit() {
    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, displayWidth(), M3Dim::STATUS_BAR_H);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_bar, 10, 0);
    lv_obj_set_style_pad_ver(s_bar, 0, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bar, barEventCb, LV_EVENT_ALL, nullptr);

    s_timeLbl = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_timeLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_timeLbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_battLbl = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_battLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_battLbl, LV_ALIGN_RIGHT_MID, 0, 0);

    s_battBody = lv_obj_create(s_bar);
    lv_obj_set_size(s_battBody, 16, 9);
    lv_obj_align(s_battBody, LV_ALIGN_RIGHT_MID, -28, 0);
    lv_obj_set_style_bg_opa(s_battBody, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_battBody, 1, 0);
    lv_obj_set_style_radius(s_battBody, 2, 0);
    lv_obj_set_style_pad_all(s_battBody, 1, 0);
    lv_obj_clear_flag(s_battBody, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_battFill = lv_obj_create(s_battBody);
    lv_obj_set_size(s_battFill, 10, 5);
    lv_obj_align(s_battFill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_battFill, lv_color_hex(M3Color::TERTIARY), 0);
    lv_obj_set_style_bg_opa(s_battFill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_battFill, 0, 0);
    lv_obj_set_style_radius(s_battFill, 1, 0);
    lv_obj_clear_flag(s_battFill, LV_OBJ_FLAG_CLICKABLE);

    s_battNub = lv_obj_create(s_bar);
    lv_obj_set_size(s_battNub, 2, 5);
    lv_obj_align(s_battNub, LV_ALIGN_RIGHT_MID, -26, 0);
    lv_obj_set_style_bg_opa(s_battNub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_battNub, 0, 0);
    lv_obj_set_style_radius(s_battNub, 1, 0);
    lv_obj_clear_flag(s_battNub, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 4; ++i) {
        s_cellBars[i] = lv_obj_create(s_bar);
        lv_obj_set_size(s_cellBars[i], 2, 3 + i * 2);
        lv_obj_align(s_cellBars[i], LV_ALIGN_RIGHT_MID, -60 + i * 4, 3 - i);
        lv_obj_set_style_bg_opa(s_cellBars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_cellBars[i], 0, 0);
        lv_obj_set_style_radius(s_cellBars[i], 1, 0);
        lv_obj_clear_flag(s_cellBars[i], LV_OBJ_FLAG_CLICKABLE);
    }

    s_wifiLbl = lv_label_create(s_bar);
    lv_label_set_text(s_wifiLbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(s_wifiLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_wifiLbl, LV_ALIGN_RIGHT_MID, -68, 0);

    s_btLbl = lv_label_create(s_bar);
    lv_label_set_text(s_btLbl, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_btLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_btLbl, LV_ALIGN_RIGHT_MID, -84, 0);

    s_airLbl = lv_label_create(s_bar);
    lv_label_set_text(s_airLbl, "AIR");
    lv_obj_set_style_text_color(s_airLbl, lv_color_hex(M3Color::AMBER), 0);
    lv_obj_set_style_text_font(s_airLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_airLbl, LV_ALIGN_RIGHT_MID, -68, 0);

    statusBarRefreshTheme();
    statusBarRefresh();
}

void statusBarRefreshTheme() {
    if (!s_bar) return;
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(m3CurrentBgHex()), 0);
    lv_color_t fg = lv_color_hex(m3TextPrimaryHex());
    lv_obj_set_style_text_color(s_timeLbl, fg, 0);
    lv_obj_set_style_text_color(s_battLbl, fg, 0);
    lv_obj_set_style_border_color(s_battBody, fg, 0);
    lv_obj_set_style_bg_color(s_battNub, fg, 0);
    lv_obj_set_style_text_color(s_wifiLbl, fg, 0);
    lv_obj_set_style_text_color(s_btLbl, lv_color_hex(m3PrimaryHex()), 0);
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_style_bg_color(s_cellBars[i], fg, 0);
    }
}

void statusBarRefresh() {
    if (!s_bar) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u", g_sys.hour, g_sys.minute);
    lv_label_set_text(s_timeLbl, buf);

    snprintf(buf, sizeof(buf), "%u%%", g_sys.batteryPct);
    lv_label_set_text(s_battLbl, buf);

    lv_coord_t fillW = (lv_coord_t)((g_sys.batteryPct * 12) / 100);
    if (fillW < 2) fillW = 2;
    lv_obj_set_width(s_battFill, fillW);
    uint32_t battCol = (g_sys.batteryPct <= 20) ? M3Color::CORAL :
                       (g_sys.batteryPct <= 40) ? M3Color::AMBER : M3Color::TERTIARY;
    lv_obj_set_style_bg_color(s_battFill, lv_color_hex(battCol), 0);

    if (g_sys.airplaneMode) {
        lv_obj_add_flag(s_wifiLbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btLbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_airLbl, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; ++i) {
            lv_obj_set_style_bg_opa(s_cellBars[i], LV_OPA_20, 0);
        }
    } else {
        lv_obj_add_flag(s_airLbl, LV_OBJ_FLAG_HIDDEN);
        if (g_sys.wifiEnabled) {
            lv_obj_clear_flag(s_wifiLbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_opa(s_wifiLbl, g_sys.wifiConnected ? LV_OPA_COVER : LV_OPA_40, 0);
        } else {
            lv_obj_add_flag(s_wifiLbl, LV_OBJ_FLAG_HIDDEN);
        }

        if (g_sys.btEnabled) lv_obj_clear_flag(s_btLbl, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_btLbl, LV_OBJ_FLAG_HIDDEN);

        int8_t bars = (g_sys.wifiEnabled && g_sys.wifiConnected) ? g_sys.wifiBars : 0;
        for (int i = 0; i < 4; ++i) {
            lv_obj_set_style_bg_opa(s_cellBars[i], (i < bars) ? LV_OPA_COVER : LV_OPA_20, 0);
        }
    }
}

void statusBarSetVisible(bool visible) {
    if (!s_bar) return;
    if (visible) lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
}
