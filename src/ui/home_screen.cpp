#include "home_screen.h"
#include "app_manager.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_widgets.h"
#include <stdio.h>

static lv_obj_t* s_home = nullptr;
static lv_obj_t* s_orb1 = nullptr;
static lv_obj_t* s_orb2 = nullptr;
static lv_obj_t* s_pager = nullptr;
static lv_obj_t* s_dots[2] = {nullptr, nullptr};
static lv_obj_t* s_atGlanceDate = nullptr;
static lv_obj_t* s_atGlanceWeather = nullptr;
static lv_obj_t* s_searchBar = nullptr;
static lv_obj_t* s_searchHint = nullptr;
static lv_obj_t* s_tileLabels[14] = {};
static uint8_t s_tileCount = 0;

static void appIconClickCb(lv_event_t* e) {
    AppId id = (AppId)(uintptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (id == APP_THEME) {
        g_sys.lightMode = !g_sys.lightMode;
        m3ApplyThemeAll();
        m3ShowToast(g_sys.lightMode ? "Light Mode" : "Dark Mode", 1100);
        return;
    }
    appManagerLaunch(id);
}

static void appIconLongPressCb(lv_event_t* e) {
    AppId id = (AppId)(uintptr_t)lv_event_get_user_data(e);
    const AppMeta* meta = appGetMeta(id);
    if (!meta) return;
    m3PulseFeedback();
    char msg[48];
    snprintf(msg, sizeof(msg), "%s  •  Tap to open", meta->name);
    m3ShowToast(msg, 1300);
}

static void atGlanceClickCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunch(APP_CLOCK);
}

static void searchClickCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunch(APP_BROWSER);
}

static void updatePageDots(uint8_t page) {
    for (int i = 0; i < 2; ++i) {
        if (!s_dots[i]) continue;
        if (i == page) {
            lv_obj_set_size(s_dots[i], 14, 5);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(m3PrimaryHex()), 0);
            lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_size(s_dots[i], 5, 5);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(m3TextSecondaryHex()), 0);
            lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_50, 0);
        }
    }
}

static void pagerScrollEndCb(lv_event_t* e) {
    (void)e;
    lv_coord_t sx = lv_obj_get_scroll_x(s_pager);
    uint8_t page = (sx > (lv_coord_t)(displayWidth() / 2)) ? 1 : 0;
    updatePageDots(page);
}

static lv_obj_t* createAppTile(lv_obj_t* parent, AppId id, int16_t x, int16_t y) {
    const AppMeta* meta = appGetMeta(id);
    if (!meta) return nullptr;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 54, 62);
    lv_obj_set_pos(wrap, x, y);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wrap, appIconClickCb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)id);
    lv_obj_add_event_cb(wrap, appIconLongPressCb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)id);

    lv_obj_t* icoBox = lv_obj_create(wrap);
    lv_obj_set_size(icoBox, 44, 44);
    lv_obj_align(icoBox, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(icoBox, lv_color_hex(meta->containerHex), 0);
    lv_obj_set_style_bg_opa(icoBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(icoBox, 1, 0);
    lv_obj_set_style_border_color(icoBox, lv_color_hex(meta->accentHex), 0);
    lv_obj_set_style_border_opa(icoBox, LV_OPA_30, 0);
    lv_obj_set_style_radius(icoBox, 14, 0);
    lv_obj_clear_flag(icoBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* ico = lv_label_create(icoBox);
    lv_label_set_text(ico, meta->icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(meta->accentHex), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_18, 0);
    lv_obj_center(ico);

    lv_obj_t* lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, meta->name);
    lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -1);

    if (s_tileCount < 14) {
        s_tileLabels[s_tileCount++] = lbl;
    }

    return wrap;
}

void homeScreenInit() {
    uint16_t w = displayWidth();
    s_tileCount = 0;

    s_home = lv_scr_act();
    lv_obj_set_style_bg_color(s_home, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_opa(s_home, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle Pixel ambient orbs
    s_orb1 = lv_obj_create(s_home);
    lv_obj_set_size(s_orb1, 180, 180);
    lv_obj_set_pos(s_orb1, -40, 20);
    lv_obj_set_style_bg_opa(s_orb1, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_orb1, 0, 0);
    lv_obj_set_style_radius(s_orb1, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(s_orb1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_orb2 = lv_obj_create(s_home);
    lv_obj_set_size(s_orb2, 160, 160);
    lv_obj_set_pos(s_orb2, 110, 130);
    lv_obj_set_style_bg_opa(s_orb2, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_orb2, 0, 0);
    lv_obj_set_style_radius(s_orb2, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(s_orb2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // 1. At-a-Glance Widget (y = 28..72)
    lv_obj_t* glance = lv_obj_create(s_home);
    lv_obj_set_size(glance, w - 24, 44);
    lv_obj_set_pos(glance, 12, 28);
    lv_obj_set_style_bg_opa(glance, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glance, 0, 0);
    lv_obj_set_style_pad_all(glance, 2, 0);
    lv_obj_clear_flag(glance, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(glance, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(glance, atGlanceClickCb, LV_EVENT_CLICKED, nullptr);

    s_atGlanceDate = lv_label_create(glance);
    lv_label_set_text(s_atGlanceDate, "Sat, Nov 21");
    lv_obj_set_style_text_font(s_atGlanceDate, &lv_font_montserrat_16, 0);
    lv_obj_align(s_atGlanceDate, LV_ALIGN_TOP_LEFT, 0, 0);

    s_atGlanceWeather = lv_label_create(glance);
    lv_label_set_text(s_atGlanceWeather, LV_SYMBOL_CHARGE "  24°C  Partly Cloudy");
    lv_obj_set_style_text_font(s_atGlanceWeather, &lv_font_montserrat_12, 0);
    lv_obj_align(s_atGlanceWeather, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    // 2. 2-Page 4x2 App Grid (y = 76..206)
    s_pager = lv_obj_create(s_home);
    lv_obj_set_size(s_pager, w, 132);
    lv_obj_set_pos(s_pager, 0, 76);
    lv_obj_set_style_bg_opa(s_pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pager, 0, 0);
    lv_obj_set_style_pad_all(s_pager, 0, 0);
    lv_obj_set_scroll_snap_x(s_pager, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_pager, LV_DIR_HOR);
    lv_obj_add_event_cb(s_pager, pagerScrollEndCb, LV_EVENT_SCROLL_END, nullptr);

    // Page 1 (8 apps)
    lv_obj_t* page1 = lv_obj_create(s_pager);
    lv_obj_set_size(page1, w, 132);
    lv_obj_set_pos(page1, 0, 0);
    lv_obj_set_style_bg_opa(page1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page1, 0, 0);
    lv_obj_set_style_pad_all(page1, 0, 0);
    lv_obj_clear_flag(page1, LV_OBJ_FLAG_SCROLLABLE);

    const AppId p1Apps[8] = {
        APP_FILES, APP_GALLERY, APP_TERMINAL, APP_WIFI,
        APP_BLUETOOTH, APP_SYSINFO, APP_CALCULATOR, APP_ABOUT
    };
    for (int i = 0; i < 8; ++i) {
        int col = i % 4;
        int row = i / 4;
        createAppTile(page1, p1Apps[i], 10 + col * 56, 2 + row * 65);
    }

    // Page 2 (6 apps: Browser, Music, Notes, Settings, Theme, Clock)
    lv_obj_t* page2 = lv_obj_create(s_pager);
    lv_obj_set_size(page2, w, 132);
    lv_obj_set_pos(page2, w, 0);
    lv_obj_set_style_bg_opa(page2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page2, 0, 0);
    lv_obj_set_style_pad_all(page2, 0, 0);
    lv_obj_clear_flag(page2, LV_OBJ_FLAG_SCROLLABLE);

    const AppId p2Apps[6] = {
        APP_BROWSER, APP_MUSIC, APP_NOTES, APP_SETTINGS,
        APP_THEME, APP_CLOCK
    };
    for (int i = 0; i < 6; ++i) {
        int col = i % 4;
        int row = i / 4;
        createAppTile(page2, p2Apps[i], 10 + col * 56, 2 + row * 65);
    }

    // 3. Page Indicator Dots (y = 212)
    lv_obj_t* dotRow = lv_obj_create(s_home);
    lv_obj_set_size(dotRow, 40, 10);
    lv_obj_set_pos(dotRow, (w - 40) / 2, 212);
    lv_obj_set_style_bg_opa(dotRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dotRow, 0, 0);
    lv_obj_set_style_pad_all(dotRow, 0, 0);
    lv_obj_set_flex_flow(dotRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dotRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dotRow, 6, 0);
    lv_obj_clear_flag(dotRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 2; ++i) {
        s_dots[i] = lv_obj_create(dotRow);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_radius(s_dots[i], M3Dim::RADIUS_PILL, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    updatePageDots(0);

    // 4. Pixel Google Search Pill (y = 226..258, cleanly above 54px bottom bar at y=266)
    s_searchBar = lv_obj_create(s_home);
    lv_obj_set_size(s_searchBar, w - 24, 32);
    lv_obj_set_pos(s_searchBar, 12, 226);
    lv_obj_set_style_bg_opa(s_searchBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_searchBar, 1, 0);
    lv_obj_set_style_border_opa(s_searchBar, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_searchBar, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(s_searchBar, 12, 0);
    lv_obj_clear_flag(s_searchBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_searchBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_searchBar, searchClickCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* gLbl = lv_label_create(s_searchBar);
    lv_label_set_text(gLbl, "G");
    lv_obj_set_style_text_color(gLbl, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(gLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(gLbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_searchHint = lv_label_create(s_searchBar);
    lv_label_set_text(s_searchHint, "Search web & apps");
    lv_obj_set_style_text_font(s_searchHint, &lv_font_montserrat_12, 0);
    lv_obj_align(s_searchHint, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t* micIco = lv_label_create(s_searchBar);
    lv_label_set_text(micIco, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(micIco, lv_color_hex(M3Color::CYAN), 0);
    lv_obj_set_style_text_font(micIco, &lv_font_montserrat_12, 0);
    lv_obj_align(micIco, LV_ALIGN_RIGHT_MID, 0, 0);

    homeScreenRefreshTheme();
}

void homeScreenRefreshTheme() {
    if (!s_home) return;
    lv_obj_set_style_bg_color(s_home, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_color(s_orb1, lv_color_hex(g_sys.lightMode ? 0xD7E3FC : 0x16243D), 0);
    lv_obj_set_style_bg_color(s_orb2, lv_color_hex(g_sys.lightMode ? 0xE8DEF8 : 0x281D3D), 0);
    lv_obj_set_style_text_color(s_atGlanceDate, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_color(s_atGlanceWeather, lv_color_hex(m3PrimaryHex()), 0);

    lv_obj_set_style_bg_color(s_searchBar, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_border_color(s_searchBar, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_text_color(s_searchHint, lv_color_hex(m3TextSecondaryHex()), 0);

    for (uint8_t i = 0; i < s_tileCount; ++i) {
        if (s_tileLabels[i]) {
            lv_obj_set_style_text_color(s_tileLabels[i], lv_color_hex(m3TextPrimaryHex()), 0);
        }
    }
    lv_coord_t sx = lv_obj_get_scroll_x(s_pager);
    updatePageDots((sx > (lv_coord_t)(displayWidth() / 2)) ? 1 : 0);
}

void homeScreenGoPage(uint8_t page) {
    if (!s_pager) return;
    lv_coord_t targetX = (page == 0) ? 0 : (lv_coord_t)displayWidth();
    lv_obj_scroll_to_x(s_pager, targetX, LV_ANIM_ON);
    updatePageDots(page);
}
