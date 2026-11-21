#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include <stdio.h>

struct Bookmark {
    const char* name;
    const char* url;
    const char* title;
    const char* summary;
};

static const Bookmark s_sites[4] = {
    { "Google",  "https://google.com",     "Google Search",        "Pixel 8 Material You Web Experience\n\nTap the URL bar above to type!" },
    { "Pixel",   "https://store.google",   "Google Pixel 8",       "Tensor G3 • Actua Display\n\nPowered by Android 14 Material 3." },
    { "News",    "https://news.google",    "Top Stories",          "• ESP32 CYD runs smooth LVGL UI\n• Material You theme shines on TFT" },
    { "Weather", "https://weather.com",    "Local Forecast",       "24°C Partly Cloudy\nHigh 26° • Low 18° • Wind 9 km/h" }
};

static lv_obj_t* s_urlTa = nullptr;
static lv_obj_t* s_chips[4] = {};
static lv_obj_t* s_pageCard = nullptr;
static lv_obj_t* s_pageTitle = nullptr;
static lv_obj_t* s_pageBody = nullptr;

static void refreshAppBrowserTheme() {
    if (s_urlTa) {
        lv_obj_set_style_bg_color(s_urlTa, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_text_color(s_urlTa, lv_color_hex(g_sys.lightMode ? m3PrimaryHex() : M3Color::CYAN), 0);
        lv_obj_set_style_border_color(s_urlTa, lv_color_hex(m3OutlineHex()), 0);
    }
    for (int i = 0; i < 4; ++i) {
        if (s_chips[i]) {
            lv_obj_set_style_bg_color(s_chips[i], lv_color_hex(m3SurfaceHighestHex()), 0);
            lv_obj_t* lbl = lv_obj_get_child(s_chips[i], 0);
            if (lbl) {
                lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
            }
        }
    }
    if (s_pageCard) {
        lv_obj_set_style_bg_color(s_pageCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_pageCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_pageTitle) {
        lv_obj_set_style_text_color(s_pageTitle, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_pageBody) {
        lv_obj_set_style_text_color(s_pageBody, lv_color_hex(m3TextSecondaryHex()), 0);
    }
}

static void browserDeleteCb(lv_event_t* e) {
    (void)e;
    s_urlTa = nullptr;
    for (int i = 0; i < 4; ++i) s_chips[i] = nullptr;
    s_pageCard = nullptr;
    s_pageTitle = nullptr;
    s_pageBody = nullptr;
}

static void onUrlEnter(const char* text) {
    if (!text || !text[0]) return;
    if (s_pageTitle) lv_label_set_text(s_pageTitle, text);
    char body[128];
    snprintf(body, sizeof(body), "Loaded: %s\n\nStatus: 200 OK (Offline Mock)\nRendered on Pixel 8 CYD.", text);
    if (s_pageBody) lv_label_set_text(s_pageBody, body);
}

static void siteClickCb(lv_event_t* e) {
    m3PulseFeedback();
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= 4) return;
    if (s_urlTa) lv_textarea_set_text(s_urlTa, s_sites[idx].url);
    if (s_pageTitle) lv_label_set_text(s_pageTitle, s_sites[idx].title);
    if (s_pageBody) lv_label_set_text(s_pageBody, s_sites[idx].summary);
}

void buildAppBrowser(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, browserDeleteCb, LV_EVENT_DELETE, nullptr);

    // Editable URL input bar -> opens custom kbd.webp keyboard on tap
    s_urlTa = lv_textarea_create(body);
    lv_obj_set_size(s_urlTa, LV_PCT(100), 34);
    lv_textarea_set_one_line(s_urlTa, true);
    lv_textarea_set_text(s_urlTa, s_sites[0].url);
    lv_obj_set_style_text_font(s_urlTa, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(s_urlTa, 1, 0);
    lv_obj_set_style_radius(s_urlTa, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(s_urlTa, 12, 0);
    lv_obj_set_style_pad_ver(s_urlTa, 7, 0);
    m3KeyboardAttach(s_urlTa, onUrlEnter);

    // Quick bookmark chips
    lv_obj_t* chipRow = lv_obj_create(body);
    lv_obj_set_size(chipRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(chipRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chipRow, 0, 0);
    lv_obj_set_style_pad_all(chipRow, 0, 0);
    lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chipRow, 5, 0);
    lv_obj_clear_flag(chipRow, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; ++i) {
        s_chips[i] = m3PillBtn(chipRow, s_sites[i].name, m3SurfaceHighestHex(), m3TextPrimaryHex(), 51, 26);
        lv_obj_add_event_cb(s_chips[i], siteClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Page content card
    s_pageCard = m3Card(body, LV_PCT(100), 108, 16);

    s_pageTitle = lv_label_create(s_pageCard);
    lv_label_set_text(s_pageTitle, s_sites[0].title);
    lv_obj_set_style_text_font(s_pageTitle, &lv_font_montserrat_16, 0);
    lv_obj_align(s_pageTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pageBody = lv_label_create(s_pageCard);
    lv_label_set_text(s_pageBody, s_sites[0].summary);
    lv_obj_set_width(s_pageBody, 196);
    lv_obj_set_style_text_font(s_pageBody, &lv_font_montserrat_12, 0);
    lv_obj_align(s_pageBody, LV_ALIGN_TOP_LEFT, 0, 24);

    refreshAppBrowserTheme();
}

REGISTER_APP_THEMED(browser, "browser", "Browser", LV_SYMBOL_GPS, 0x78D9EC, 0x173F49, buildAppBrowser, nullptr, refreshAppBrowserTheme, 90);
