#include "home_screen.h"
#include "app_manager.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_widgets.h"
#include <stdio.h>

static lv_obj_t* s_home = nullptr;
static lv_obj_t* s_pager = nullptr;
static lv_obj_t* s_dots[2] = {nullptr, nullptr};
static lv_obj_t* s_atGlanceTime = nullptr;
static uint8_t s_curPage = 0;

static void appIconClickCb(lv_event_t* e) {
    AppId id = (AppId)(uintptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
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
    s_curPage = page;
    for (int i = 0; i < 2; ++i) {
        if (!s_dots[i]) continue;
        if (i == page) {
            lv_obj_set_size(s_dots[i], 14, 5);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(M3Color::PRIMARY), 0);
            lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_size(s_dots[i], 5, 5);
            lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(M3Color::TEXT_MUTED), 0);
            lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_60, 0);
        }
    }
}

static void pagerScrollEndCb(lv_event_t* e) {
    (void)e;
    lv_coord_t sx = lv_obj_get_scroll_x(s_pager);
    uint8_t page = (sx > (lv_coord_t)(displayWidth() / 2)) ? 1 : 0;
    updatePageDots(page);
}

static lv_obj_t* createAppTile(lv_obj_t* parent, AppId id, int16_t x, int16_t y, bool showLabel) {
    const AppMeta* meta = appGetMeta(id);
    if (!meta) return nullptr;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 54, showLabel ? 62 : 46);
    lv_obj_set_pos(wrap, x, y);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wrap, appIconClickCb, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)id);
    lv_obj_add_event_cb(wrap, appIconLongPressCb, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)id);

    // M3 Squircle icon container
    lv_obj_t* icoBox = lv_obj_create(wrap);
    lv_obj_set_size(icoBox, 42, 42);
    lv_obj_align(icoBox, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(icoBox, lv_color_hex(meta->containerHex), 0);
    lv_obj_set_style_bg_opa(icoBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(icoBox, 1, 0);
    lv_obj_set_style_border_color(icoBox, lv_color_hex(meta->accentHex), 0);
    lv_obj_set_style_border_opa(icoBox, LV_OPA_30, 0);
    lv_obj_set_style_radius(icoBox, 14, 0);
    lv_obj_clear_flag(icoBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Pressed feedback on parent
    lv_obj_t* ico = lv_label_create(icoBox);
    lv_label_set_text(ico, meta->icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(meta->accentHex), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_18, 0);
    lv_obj_center(ico);

    if (showLabel) {
        lv_obj_t* lbl = lv_label_create(wrap);
        lv_label_set_text(lbl, meta->name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    return wrap;
}

void homeScreenInit() {
    uint16_t w = displayWidth();
    uint16_t h = displayHeight();

    s_home = lv_scr_act();
    lv_obj_set_style_bg_color(s_home, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_opa(s_home, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle Pixel Wallpaper Ambient Orbs (lightweight circular tonal accents)
    lv_obj_t* orb1 = lv_obj_create(s_home);
    lv_obj_set_size(orb1, 180, 180);
    lv_obj_set_pos(orb1, -40, 20);
    lv_obj_set_style_bg_color(orb1, lv_color_hex(0x16243D), 0);
    lv_obj_set_style_bg_opa(orb1, LV_OPA_40, 0);
    lv_obj_set_style_border_width(orb1, 0, 0);
    lv_obj_set_style_radius(orb1, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(orb1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* orb2 = lv_obj_create(s_home);
    lv_obj_set_size(orb2, 160, 160);
    lv_obj_set_pos(orb2, 110, 130);
    lv_obj_set_style_bg_color(orb2, lv_color_hex(0x281D3D), 0);
    lv_obj_set_style_bg_opa(orb2, LV_OPA_40, 0);
    lv_obj_set_style_border_width(orb2, 0, 0);
    lv_obj_set_style_radius(orb2, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(orb2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // 1. Pixel At-a-Glance Widget (y = 28..74)
    lv_obj_t* glance = lv_obj_create(s_home);
    lv_obj_set_size(glance, w - 24, 44);
    lv_obj_set_pos(glance, 12, 28);
    lv_obj_set_style_bg_opa(glance, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glance, 0, 0);
    lv_obj_set_style_pad_all(glance, 2, 0);
    lv_obj_clear_flag(glance, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(glance, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(glance, atGlanceClickCb, LV_EVENT_CLICKED, nullptr);

    s_atGlanceTime = lv_label_create(glance);
    lv_label_set_text(s_atGlanceTime, "Sat, Nov 21");
    lv_obj_set_style_text_color(s_atGlanceTime, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_atGlanceTime, &lv_font_montserrat_16, 0);
    lv_obj_align(s_atGlanceTime, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* weatherLbl = lv_label_create(glance);
    lv_label_set_text(weatherLbl, LV_SYMBOL_CHARGE "  24°C  Partly Cloudy");
    lv_obj_set_style_text_color(weatherLbl, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(weatherLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(weatherLbl, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    // 2. Horizontal 2-Page App Grid (y = 76..206)
    s_pager = lv_obj_create(s_home);
    lv_obj_set_size(s_pager, w, 130);
    lv_obj_set_pos(s_pager, 0, 76);
    lv_obj_set_style_bg_opa(s_pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pager, 0, 0);
    lv_obj_set_style_pad_all(s_pager, 0, 0);
    lv_obj_set_scroll_snap_x(s_pager, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_pager, LV_DIR_HOR);
    lv_obj_add_event_cb(s_pager, pagerScrollEndCb, LV_EVENT_SCROLL_END, nullptr);

    // Page 1 (8 apps: Files, Gallery, Terminal, Wi-Fi, Bluetooth, SysInfo, Calc, About)
    lv_obj_t* page1 = lv_obj_create(s_pager);
    lv_obj_set_size(page1, w, 130);
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
        createAppTile(page1, p1Apps[i], 10 + col * 56, 2 + row * 64, true);
    }

    // Page 2 (Clock + quick access to primary apps)
    lv_obj_t* page2 = lv_obj_create(s_pager);
    lv_obj_set_size(page2, w, 130);
    lv_obj_set_pos(page2, w, 0);
    lv_obj_set_style_bg_opa(page2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page2, 0, 0);
    lv_obj_set_style_pad_all(page2, 0, 0);
    lv_obj_clear_flag(page2, LV_OBJ_FLAG_SCROLLABLE);

    const AppId p2Apps[5] = {
        APP_CLOCK, APP_BROWSER, APP_MUSIC, APP_NOTES, APP_SETTINGS
    };
    for (int i = 0; i < 5; ++i) {
        int col = i % 4;
        int row = i / 4;
        createAppTile(page2, p2Apps[i], 10 + col * 56, 2 + row * 64, true);
    }

    // 3. Page Indicator Dots (y = 208)
    lv_obj_t* dotRow = lv_obj_create(s_home);
    lv_obj_set_size(dotRow, 40, 10);
    lv_obj_set_pos(dotRow, (w - 40) / 2, 208);
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

    // 4. Pixel Google Search Pill (y = 222..252)
    lv_obj_t* searchBar = lv_obj_create(s_home);
    lv_obj_set_size(searchBar, w - 24, 30);
    lv_obj_set_pos(searchBar, 12, 222);
    lv_obj_set_style_bg_color(searchBar, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(searchBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(searchBar, 1, 0);
    lv_obj_set_style_border_color(searchBar, lv_color_hex(M3Color::OUTLINE), 0);
    lv_obj_set_style_border_opa(searchBar, LV_OPA_50, 0);
    lv_obj_set_style_radius(searchBar, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(searchBar, 12, 0);
    lv_obj_clear_flag(searchBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(searchBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(searchBar, searchClickCb, LV_EVENT_CLICKED, nullptr);

    // Colorful "G" badge
    lv_obj_t* gLbl = lv_label_create(searchBar);
    lv_label_set_text(gLbl, "G");
    lv_obj_set_style_text_color(gLbl, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(gLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(gLbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* sHint = lv_label_create(searchBar);
    lv_label_set_text(sHint, "Search web & apps");
    lv_obj_set_style_text_color(sHint, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(sHint, &lv_font_montserrat_12, 0);
    lv_obj_align(sHint, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t* micIco = lv_label_create(searchBar);
    lv_label_set_text(micIco, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(micIco, lv_color_hex(M3Color::CYAN), 0);
    lv_obj_set_style_text_font(micIco, &lv_font_montserrat_12, 0);
    lv_obj_align(micIco, LV_ALIGN_RIGHT_MID, 0, 0);

    // 5. Bottom Dock with 4 Primary Apps (y = 258..314)
    lv_obj_t* dock = lv_obj_create(s_home);
    lv_obj_set_size(dock, w - 16, 54);
    lv_obj_set_pos(dock, 8, h - 60);
    lv_obj_set_style_bg_color(dock, lv_color_hex(M3Color::SURFACE), 0);
    lv_obj_set_style_bg_opa(dock, LV_OPA_80, 0);
    lv_obj_set_style_border_width(dock, 1, 0);
    lv_obj_set_style_border_color(dock, lv_color_hex(M3Color::OUTLINE), 0);
    lv_obj_set_style_border_opa(dock, LV_OPA_40, 0);
    lv_obj_set_style_radius(dock, 20, 0);
    lv_obj_set_style_pad_all(dock, 0, 0);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);

    const AppId dockApps[4] = { APP_BROWSER, APP_MUSIC, APP_NOTES, APP_SETTINGS };
    for (int i = 0; i < 4; ++i) {
        createAppTile(dock, dockApps[i], 4 + i * 54, 5, false);
    }
}

void homeScreenRefreshTheme() {
    if (!s_home) return;
    lv_obj_set_style_bg_color(s_home, lv_color_hex(m3CurrentBgHex()), 0);
}
