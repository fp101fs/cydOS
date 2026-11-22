#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../launcher/launcher_app_registry.h"
#include "../launcher/launcher_home_sync.h"
#include <cstdio>

// App Store shell (registered at Home Screen position #1, order = 0).
// Full tabbed UI (Explore, SD Card, Installed, Tools) is built in Items 6-11.

static lv_obj_t* s_storeBody = nullptr;
static lv_obj_t* s_summaryCard = nullptr;
static lv_obj_t* s_summaryTitle = nullptr;
static lv_obj_t* s_summarySub = nullptr;

static void refreshBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    launcherHomeSyncRefresh(true, false);
    if (s_summarySub) {
        char buf[64];
        uint8_t n = appGetDynamicCount();
        snprintf(buf, sizeof(buf), "%u installed %s synced to Home Screen",
                 (unsigned)n, (n == 1) ? "App" : "Apps");
        lv_label_set_text(s_summarySub, buf);
    }
    m3ShowToast("App list refreshed", 1200);
}

static void buildAppStore(lv_obj_t* body) {
    s_storeBody = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);

    s_summaryCard = m3Card(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_summaryCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_summaryCard, 6, 0);

    s_summaryTitle = lv_label_create(s_summaryCard);
    lv_label_set_text(s_summaryTitle, LV_SYMBOL_DOWNLOAD "  Pixel CYD App Store");
    lv_obj_set_style_text_color(s_summaryTitle, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(s_summaryTitle, &lv_font_montserrat_14, 0);

    s_summarySub = lv_label_create(s_summaryCard);
    char buf[64];
    uint8_t n = appGetDynamicCount();
    snprintf(buf, sizeof(buf), "%u installed %s synced to Home Screen",
             (unsigned)n, (n == 1) ? "App" : "Apps");
    lv_label_set_text(s_summarySub, buf);
    lv_obj_set_width(s_summarySub, lv_pct(100));
    lv_label_set_long_mode(s_summarySub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_summarySub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(s_summarySub, &lv_font_montserrat_12, 0);

    lv_obj_t* refreshBtn = lv_btn_create(s_summaryCard);
    lv_obj_set_height(refreshBtn, 32);
    lv_obj_set_style_radius(refreshBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(refreshBtn, lv_color_hex(M3Color::PRIMARY_CONT), 0);
    lv_obj_set_style_shadow_width(refreshBtn, 0, 0);
    lv_obj_add_event_cb(refreshBtn, refreshBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnLbl = lv_label_create(refreshBtn);
    lv_label_set_text(btnLbl, LV_SYMBOL_REFRESH "  Sync Installed Apps");
    lv_obj_set_style_text_color(btnLbl, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(btnLbl);
}

static void refreshAppStoreTheme() {
    if (!s_storeBody) return;
    if (s_summaryCard) {
        lv_obj_set_style_bg_color(s_summaryCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_summaryCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_summaryTitle) {
        lv_obj_set_style_text_color(s_summaryTitle, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_summarySub) {
        lv_obj_set_style_text_color(s_summarySub, lv_color_hex(m3TextSecondaryHex()), 0);
    }
}

REGISTER_APP_THEMED(app_store, "app_store", "App Store", LV_SYMBOL_DOWNLOAD,
                    M3Color::PRIMARY, 0x1A73E8,
                    buildAppStore, nullptr, refreshAppStoreTheme, 0);
