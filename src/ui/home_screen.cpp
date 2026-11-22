#include "home_screen.h"
#include "app_manager.h"
#include "../apps/app_registry.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../drivers/time_mgr.h"
#include "../components/m3_widgets.h"
#include "../launcher/launcher_home_sync.h"
#include <stdio.h>

static constexpr uint8_t APPS_PER_PAGE = 8;
static constexpr uint8_t MAX_PAGES = (APP_MAX_COUNT + APPS_PER_PAGE - 1) / APPS_PER_PAGE;

static constexpr int16_t GLANCE_Y     = 26;
static constexpr int16_t GLANCE_H     = 26;
static constexpr int16_t PAGER_Y      = 56;
static constexpr int16_t PAGER_H      = 152;
static constexpr int16_t TILE_W       = 56;
static constexpr int16_t TILE_H       = 70;
static constexpr int16_t ICON_BOX_SZ  = 52;
static constexpr int16_t TILE_STEP_X  = 58;
static constexpr int16_t TILE_START_Y = 3;
static constexpr int16_t TILE_STEP_Y  = 76;

static lv_obj_t* s_home = nullptr;
static lv_obj_t* s_orb1 = nullptr;
static lv_obj_t* s_orb2 = nullptr;
static lv_obj_t* s_pager = nullptr;
static lv_obj_t* s_dotRow = nullptr;
static lv_obj_t* s_dots[MAX_PAGES] = {};
static uint8_t s_pageCount = 1;
static uint8_t s_currentPage = 0;
static lv_obj_t* s_atGlanceDate = nullptr;
static lv_obj_t* s_weatherSun = nullptr;
static lv_obj_t* s_weatherCloud = nullptr;
static lv_obj_t* s_atGlanceWeather = nullptr;
static lv_obj_t* s_searchBar = nullptr;
static lv_obj_t* s_searchHint = nullptr;
static lv_obj_t* s_tileLabels[APP_MAX_COUNT] = {};
static uint8_t s_tileCount = 0;

static void appIconClickCb(lv_event_t* e) {
    const AppMeta* meta = (const AppMeta*)lv_event_get_user_data(e);
    if (!meta) return;
    m3PulseFeedback();
    if (appIsDynamic(meta)) {
        launcherHomeLaunchApp(meta->launcherLabel);
        return;
    }
    appManagerLaunch(meta);
}

static void appIconLongPressCb(lv_event_t* e) {
    const AppMeta* meta = (const AppMeta*)lv_event_get_user_data(e);
    if (!meta) return;
    m3PulseFeedback();
    if (appIsDynamic(meta)) {
        launcherHomeShowAppActions(meta->launcherLabel, meta->name);
        return;
    }
    char msg[48];
    snprintf(msg, sizeof(msg), "%s  •  Tap to open", meta->name);
    m3ShowToast(msg, 1300);
}

static void atGlanceClickCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunchById("clock");
}

static void searchClickCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunchById("browser");
}

static void updatePageDots(uint8_t page) {
    if (s_pageCount == 0) return;
    if (page >= s_pageCount) page = s_pageCount - 1;
    s_currentPage = page;
    for (uint8_t i = 0; i < s_pageCount; ++i) {
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

static uint8_t computeCurrentPage() {
    if (!s_pager || s_pageCount <= 1) return 0;
    lv_coord_t w = (lv_coord_t)displayWidth();
    if (w <= 0) return 0;
    lv_coord_t sx = lv_obj_get_scroll_x(s_pager);
    if (sx < 0) sx = 0;
    uint8_t page = (uint8_t)((sx + w / 2) / w);
    if (page >= s_pageCount) page = s_pageCount - 1;
    return page;
}

static void pagerScrollEndCb(lv_event_t* e) {
    (void)e;
    updatePageDots(computeCurrentPage());
}

static lv_obj_t* createAppTile(lv_obj_t* parent, const AppMeta* meta, int16_t x, int16_t y) {
    if (!meta) return nullptr;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, TILE_W, TILE_H);
    lv_obj_set_pos(wrap, x, y);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wrap, appIconClickCb, LV_EVENT_CLICKED, (void*)meta);
    lv_obj_add_event_cb(wrap, appIconLongPressCb, LV_EVENT_LONG_PRESSED, (void*)meta);

    if (meta->iconImg) {
        lv_obj_t* img = lv_img_create(wrap);
        lv_img_set_src(img, meta->iconImg);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t* icoBox = lv_obj_create(wrap);
        lv_obj_set_size(icoBox, ICON_BOX_SZ, ICON_BOX_SZ);
        lv_obj_align(icoBox, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(icoBox, lv_color_hex(meta->containerHex), 0);
        lv_obj_set_style_bg_opa(icoBox, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(icoBox, 0, 0);
        lv_obj_set_style_radius(icoBox, M3Dim::RADIUS_CARD, 0);
        lv_obj_clear_flag(icoBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* ico = lv_label_create(icoBox);
        lv_label_set_text(ico, meta->icon);
        lv_obj_set_style_text_color(ico, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
        lv_obj_center(ico);

        if (appIsDynamic(meta)) {
            lv_obj_t* badge = lv_obj_create(icoBox);
            lv_obj_set_size(badge, 8, 8);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 2, -2);
            lv_obj_set_style_bg_color(badge, lv_color_hex(meta->accentHex), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(badge, 1, 0);
            lv_obj_set_style_border_color(badge, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_radius(badge, M3Dim::RADIUS_PILL, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_obj_t* lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, meta->name);
    lv_obj_set_width(lbl, TILE_W);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -1);

    if (s_tileCount < APP_MAX_COUNT) {
        s_tileLabels[s_tileCount++] = lbl;
    }

    return wrap;
}

static void buildAppGrid(bool goFirstPage) {
    if (!s_pager || !s_dotRow) return;

    uint16_t w = displayWidth();
    uint8_t prevPage = goFirstPage ? 0 : s_currentPage;

    lv_obj_clean(s_pager);
    lv_obj_clean(s_dotRow);
    s_tileCount = 0;
    for (uint8_t i = 0; i < APP_MAX_COUNT; ++i) s_tileLabels[i] = nullptr;
    for (uint8_t i = 0; i < MAX_PAGES; ++i) s_dots[i] = nullptr;

    uint8_t totalApps = appGetCount();
    s_pageCount = (totalApps == 0) ? 1 : (uint8_t)((totalApps + APPS_PER_PAGE - 1) / APPS_PER_PAGE);
    if (s_pageCount > MAX_PAGES) s_pageCount = MAX_PAGES;

    int16_t gridW = 3 * TILE_STEP_X + TILE_W;
    int16_t tileStartX = (int16_t)(((int16_t)w - gridW) / 2);

    for (uint8_t p = 0; p < s_pageCount; ++p) {
        lv_obj_t* pageObj = lv_obj_create(s_pager);
        lv_obj_set_size(pageObj, w, PAGER_H);
        lv_obj_set_pos(pageObj, (lv_coord_t)(p * w), 0);
        lv_obj_set_style_bg_opa(pageObj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pageObj, 0, 0);
        lv_obj_set_style_pad_all(pageObj, 0, 0);
        lv_obj_clear_flag(pageObj, LV_OBJ_FLAG_SCROLLABLE);

        uint8_t startIdx = p * APPS_PER_PAGE;
        for (uint8_t i = 0; i < APPS_PER_PAGE && (startIdx + i) < totalApps; ++i) {
            const AppMeta* meta = appGetAt(startIdx + i);
            int col = i % 4;
            int row = i / 4;
            createAppTile(pageObj, meta, (int16_t)(tileStartX + col * TILE_STEP_X),
                          (int16_t)(TILE_START_Y + row * TILE_STEP_Y));
        }
    }

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        s_dots[i] = lv_obj_create(s_dotRow);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_radius(s_dots[i], M3Dim::RADIUS_PILL, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }

    if (prevPage >= s_pageCount) prevPage = s_pageCount - 1;
    lv_obj_scroll_to_x(s_pager, (lv_coord_t)(prevPage * w), LV_ANIM_OFF);
    updatePageDots(prevPage);
}

void homeScreenInit() {
    uint16_t w = displayWidth();
    s_tileCount = 0;
    s_currentPage = 0;

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

    // 1. Compact single-line At-a-Glance Widget (y = 26..52)
    lv_obj_t* glance = lv_obj_create(s_home);
    lv_obj_set_size(glance, w - 24, GLANCE_H);
    lv_obj_set_pos(glance, 12, GLANCE_Y);
    lv_obj_set_style_bg_opa(glance, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glance, 0, 0);
    lv_obj_set_style_pad_all(glance, 0, 0);
    lv_obj_set_style_pad_left(glance, 2, 0);
    lv_obj_set_flex_flow(glance, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(glance, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(glance, 10, 0);
    lv_obj_clear_flag(glance, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(glance, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(glance, atGlanceClickCb, LV_EVENT_CLICKED, nullptr);

    s_atGlanceDate = lv_label_create(glance);
    lv_obj_set_style_text_font(s_atGlanceDate, &lv_font_montserrat_16, 0);

    // Inline weather cluster (simple sun+cloud icon + temperature) just to the right of date
    lv_obj_t* weatherGroup = lv_obj_create(glance);
    lv_obj_set_size(weatherGroup, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(weatherGroup, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weatherGroup, 0, 0);
    lv_obj_set_style_pad_all(weatherGroup, 0, 0);
    lv_obj_set_flex_flow(weatherGroup, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weatherGroup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weatherGroup, 4, 0);
    lv_obj_clear_flag(weatherGroup, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* weatherIco = lv_obj_create(weatherGroup);
    lv_obj_set_size(weatherIco, 16, 14);
    lv_obj_set_style_bg_opa(weatherIco, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weatherIco, 0, 0);
    lv_obj_set_style_pad_all(weatherIco, 0, 0);
    lv_obj_clear_flag(weatherIco, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_weatherSun = lv_obj_create(weatherIco);
    lv_obj_set_size(s_weatherSun, 9, 9);
    lv_obj_set_pos(s_weatherSun, 0, 1);
    lv_obj_set_style_bg_opa(s_weatherSun, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_weatherSun, 0, 0);
    lv_obj_set_style_radius(s_weatherSun, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(s_weatherSun, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_weatherCloud = lv_obj_create(weatherIco);
    lv_obj_set_size(s_weatherCloud, 11, 6);
    lv_obj_set_pos(s_weatherCloud, 5, 7);
    lv_obj_set_style_bg_opa(s_weatherCloud, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_weatherCloud, 0, 0);
    lv_obj_set_style_radius(s_weatherCloud, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(s_weatherCloud, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_atGlanceWeather = lv_label_create(weatherGroup);
    lv_label_set_text(s_atGlanceWeather, "24°C");
    lv_obj_set_style_text_font(s_atGlanceWeather, &lv_font_montserrat_14, 0);

    // 2. Dynamic N-Page 4x2 App Grid (y = 56..208)
    s_pager = lv_obj_create(s_home);
    lv_obj_set_size(s_pager, w, PAGER_H);
    lv_obj_set_pos(s_pager, 0, PAGER_Y);
    lv_obj_set_style_bg_opa(s_pager, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pager, 0, 0);
    lv_obj_set_style_pad_all(s_pager, 0, 0);
    lv_obj_set_scroll_snap_x(s_pager, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_pager, LV_DIR_HOR);
    lv_obj_add_event_cb(s_pager, pagerScrollEndCb, LV_EVENT_SCROLL_END, nullptr);

    // 3. Page Indicator Dots (y = 212)
    s_dotRow = lv_obj_create(s_home);
    lv_obj_set_size(s_dotRow, w, 10);
    lv_obj_set_pos(s_dotRow, 0, 212);
    lv_obj_set_style_bg_opa(s_dotRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dotRow, 0, 0);
    lv_obj_set_style_pad_all(s_dotRow, 0, 0);
    lv_obj_set_flex_flow(s_dotRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dotRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dotRow, 6, 0);
    lv_obj_clear_flag(s_dotRow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    buildAppGrid(true);

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

    homeScreenRefreshDate();
    homeScreenRefreshTheme();
}

void homeScreenRefreshDate() {
    if (!s_atGlanceDate) return;
    char buf[24];
    timeMgrFormatShortDate(buf, sizeof(buf));
    lv_label_set_text(s_atGlanceDate, buf);
}

void homeScreenRefreshTheme() {
    if (!s_home) return;
    lv_obj_set_style_bg_color(s_home, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_color(s_orb1, lv_color_hex(g_sys.lightMode ? 0xD7E3FC : 0x16243D), 0);
    lv_obj_set_style_bg_color(s_orb2, lv_color_hex(g_sys.lightMode ? 0xE8DEF8 : 0x281D3D), 0);
    lv_obj_set_style_text_color(s_atGlanceDate, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_weatherSun) {
        lv_obj_set_style_bg_color(s_weatherSun, lv_color_hex(g_sys.lightMode ? 0xF59E0B : M3Color::AMBER), 0);
    }
    if (s_weatherCloud) {
        lv_obj_set_style_bg_color(s_weatherCloud, lv_color_hex(g_sys.lightMode ? 0x94A3B8 : M3Color::TEXT_PRIMARY), 0);
    }
    lv_obj_set_style_text_color(s_atGlanceWeather, lv_color_hex(m3PrimaryHex()), 0);

    lv_obj_set_style_bg_color(s_searchBar, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_border_color(s_searchBar, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_text_color(s_searchHint, lv_color_hex(m3TextSecondaryHex()), 0);

    for (uint8_t i = 0; i < s_tileCount; ++i) {
        if (s_tileLabels[i]) {
            lv_obj_set_style_text_color(s_tileLabels[i], lv_color_hex(m3TextPrimaryHex()), 0);
        }
    }
    updatePageDots(computeCurrentPage());
}

void homeScreenGoPage(uint8_t page) {
    if (!s_pager || s_pageCount == 0) return;
    if (page >= s_pageCount) page = s_pageCount - 1;
    lv_coord_t targetX = (lv_coord_t)(page * displayWidth());
    lv_obj_scroll_to_x(s_pager, targetX, LV_ANIM_ON);
    updatePageDots(page);
}

void homeScreenRebuildGrid(bool goFirstPage) {
    if (!s_home || !s_pager || !s_dotRow) return;
    buildAppGrid(goFirstPage);
}
