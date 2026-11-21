#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

void buildAppAbout(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    // Hero Pixel 8 card
    lv_obj_t* hero = m3Card(body, LV_PCT(100), 74, 18);
    lv_obj_set_style_bg_color(hero, lv_color_hex(M3Color::TERTIARY_CONT), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(M3Color::TERTIARY), 0);

    lv_obj_t* devLbl = lv_label_create(hero);
    lv_label_set_text(devLbl, "Pixel 8");
    lv_obj_set_style_text_color(devLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(devLbl, &lv_font_montserrat_20, 0);
    lv_obj_align(devLbl, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* osLbl = lv_label_create(hero);
    lv_label_set_text(osLbl, "Android 14  •  CYD Edition");
    lv_obj_set_style_text_color(osLbl, lv_color_hex(M3Color::TERTIARY), 0);
    lv_obj_set_style_text_font(osLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(osLbl, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    lv_obj_t* info = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 2, 0);

    m3InfoRow(info, "Model", "ESP32-2432S028");
    m3InfoRow(info, "UI Engine", "LVGL 8.3.11");
    m3InfoRow(info, "Design", "Material You (M3)");
    m3InfoRow(info, "Display", "240x320 ST7789");
    m3InfoRow(info, "Touch", "XPT2046 Resistive");
}
