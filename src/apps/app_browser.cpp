#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

struct Bookmark {
    const char* name;
    const char* url;
    const char* title;
    const char* summary;
};

static const Bookmark s_sites[4] = {
    { "Google",  "https://google.com",     "Google Search",        "Pixel 8 Material You Web Experience\n\nFast, clean, and private mobile browsing." },
    { "Pixel",   "https://store.google",   "Google Pixel 8",       "Tensor G3 • Actua Display\n\nPowered by Android 14 Material 3." },
    { "News",    "https://news.google",    "Top Stories",          "• ESP32 CYD runs 60fps LVGL UI\n• Material You dark mode shines on TFT" },
    { "Weather", "https://weather.com",    "Local Forecast",       "24°C Partly Cloudy\nHigh 26° • Low 18° • Wind 9 km/h" }
};

static lv_obj_t* s_urlLbl = nullptr;
static lv_obj_t* s_pageTitle = nullptr;
static lv_obj_t* s_pageBody = nullptr;

static void siteClickCb(lv_event_t* e) {
    m3PulseFeedback();
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= 4) return;
    lv_label_set_text(s_urlLbl, s_sites[idx].url);
    lv_label_set_text(s_pageTitle, s_sites[idx].title);
    lv_label_set_text(s_pageBody, s_sites[idx].summary);
}

void buildAppBrowser(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    // URL pill bar
    lv_obj_t* urlBar = m3Card(body, LV_PCT(100), 34, M3Dim::RADIUS_PILL);
    lv_obj_set_style_pad_hor(urlBar, 12, 0);
    s_urlLbl = lv_label_create(urlBar);
    lv_label_set_text(s_urlLbl, s_sites[0].url);
    lv_obj_set_style_text_color(s_urlLbl, lv_color_hex(M3Color::CYAN), 0);
    lv_obj_set_style_text_font(s_urlLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_urlLbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Quick bookmark chips
    lv_obj_t* chipRow = lv_obj_create(body);
    lv_obj_set_size(chipRow, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(chipRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chipRow, 0, 0);
    lv_obj_set_style_pad_all(chipRow, 0, 0);
    lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chipRow, 5, 0);
    lv_obj_clear_flag(chipRow, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t* btn = m3PillBtn(chipRow, s_sites[i].name, M3Color::SURFACE_HIGHEST, M3Color::TEXT_PRIMARY, 51, 28);
        lv_obj_add_event_cb(btn, siteClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Page content card
    lv_obj_t* page = m3Card(body, LV_PCT(100), 132, 16);
    lv_obj_set_style_bg_color(page, lv_color_hex(M3Color::SURFACE), 0);

    s_pageTitle = lv_label_create(page);
    lv_label_set_text(s_pageTitle, s_sites[0].title);
    lv_obj_set_style_text_color(s_pageTitle, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(s_pageTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(s_pageTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pageBody = lv_label_create(page);
    lv_label_set_text(s_pageBody, s_sites[0].summary);
    lv_obj_set_width(s_pageBody, 196);
    lv_obj_set_style_text_color(s_pageBody, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_pageBody, &lv_font_montserrat_12, 0);
    lv_obj_align(s_pageBody, LV_ALIGN_TOP_LEFT, 0, 26);
}
