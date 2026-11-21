#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <Arduino.h>
#include <stdio.h>

static lv_obj_t* s_clockBig = nullptr;
static uint32_t s_lastSecTick = 0;

static void refreshClock() {
    if (!s_clockBig) return;
    uint8_t sec = (uint8_t)((millis() / 1000) % 60);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", g_sys.hour, g_sys.minute, sec);
    lv_label_set_text(s_clockBig, buf);
}

void buildAppClock(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    lv_obj_t* timeCard = m3Card(body, LV_PCT(100), 66, 16);
    lv_obj_set_style_bg_color(timeCard, lv_color_hex(M3Color::PRIMARY_CONT), 0);
    lv_obj_set_style_border_color(timeCard, lv_color_hex(M3Color::PRIMARY), 0);

    s_clockBig = lv_label_create(timeCard);
    lv_obj_set_style_text_color(s_clockBig, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_clockBig, &lv_font_montserrat_28, 0);
    lv_obj_align(s_clockBig, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t* sub = lv_label_create(timeCard);
    lv_label_set_text(sub, "Saturday, November 21, 2026");
    lv_obj_set_style_text_color(sub, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, 2);

    lv_obj_t* cal = lv_calendar_create(body);
    lv_obj_set_size(cal, LV_PCT(100), 140);
    lv_calendar_set_today_date(cal, 2026, 11, 21);
    lv_calendar_set_showed_date(cal, 2026, 11);
    lv_obj_set_style_bg_color(cal, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_border_width(cal, 1, 0);
    lv_obj_set_style_border_color(cal, lv_color_hex(M3Color::OUTLINE), 0);
    lv_obj_set_style_radius(cal, 14, 0);

    s_lastSecTick = 0;
    refreshClock();
}

void tickAppClock() {
    uint32_t now = millis();
    if (now - s_lastSecTick >= 1000) {
        s_lastSecTick = now;
        refreshClock();
    }
}
