#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

namespace {

constexpr uint8_t SQL_CHART_PTS = 20;
constexpr uint8_t SQL_STRESS_ARCS = 4;
constexpr uint8_t SQL_STRESS_BARS = 4;
constexpr uint8_t SQL_STRESS_BADGES = 4;
constexpr uint32_t SQL_HUD_WINDOW_MS = 500;
constexpr uint32_t SQL_ANIM_INTERVAL_MS = 33; // ~30 Hz animation step

static const lv_font_t* const kFontLadder[] = {
    &lv_font_montserrat_12,
    &lv_font_montserrat_16,
    &lv_font_montserrat_20,
    &lv_font_montserrat_24,
    &lv_font_montserrat_28,
    &lv_font_montserrat_36,
};
static const uint8_t kFontSizes[] = { 12, 16, 20, 24, 28, 36 };
constexpr uint8_t kFontCount = sizeof(kFontLadder) / sizeof(kFontLadder[0]);

// Persistent UI pointers (nulled in LV_EVENT_DELETE)
static lv_obj_t* s_body = nullptr;
static lv_obj_t* s_hudCard = nullptr;
static lv_obj_t* s_fpsLbl = nullptr;
static lv_obj_t* s_heapLbl = nullptr;
static lv_obj_t* s_stressSw = nullptr;

static lv_obj_t* s_heroCard = nullptr;
static lv_obj_t* s_arcSpeed = nullptr;
static lv_obj_t* s_arcSpeedLbl = nullptr;
static lv_obj_t* s_arcLoad = nullptr;
static lv_obj_t* s_arcLoadLbl = nullptr;

static lv_obj_t* s_glassWrap = nullptr;
static lv_obj_t* s_glassOverlay = nullptr;
static lv_obj_t* s_glassTitle = nullptr;
static lv_obj_t* s_glassSub = nullptr;
static lv_obj_t* s_opaSlider = nullptr;

static lv_obj_t* s_chartCard = nullptr;
static lv_obj_t* s_chartTitle = nullptr;
static lv_obj_t* s_chartPeakLbl = nullptr;
static lv_obj_t* s_chart = nullptr;
static lv_chart_series_t* s_serA = nullptr;
static lv_chart_series_t* s_serB = nullptr;

static lv_obj_t* s_fontCard = nullptr;
static lv_obj_t* s_fontSpecLbl = nullptr;
static lv_obj_t* s_fontMetaLbl = nullptr;

static lv_obj_t* s_stressCard = nullptr;
static lv_obj_t* s_stressTitle = nullptr;
static lv_obj_t* s_stressGrid = nullptr;
static lv_obj_t* s_stressArcs[SQL_STRESS_ARCS] = {};
static lv_obj_t* s_stressBars[SQL_STRESS_BARS] = {};
static lv_obj_t* s_stressBadges[SQL_STRESS_BADGES] = {};
static lv_obj_t* s_stressBadgeLbls[SQL_STRESS_BADGES] = {};

// Runtime benchmark state
static bool s_stressOn = false;
static uint8_t s_fontIdx = 3; // starts at 24px
static uint8_t s_glassOpa = 155;
static uint32_t s_phase = 0;
static uint32_t s_lastAnimMs = 0;
static uint32_t s_hudWindowStartMs = 0;
static uint32_t s_hudFrames = 0;
static uint16_t s_lastFps = 0;

static void clearStressPointers() {
    s_stressGrid = nullptr;
    for (uint8_t i = 0; i < SQL_STRESS_ARCS; ++i) s_stressArcs[i] = nullptr;
    for (uint8_t i = 0; i < SQL_STRESS_BARS; ++i) s_stressBars[i] = nullptr;
    for (uint8_t i = 0; i < SQL_STRESS_BADGES; ++i) {
        s_stressBadges[i] = nullptr;
        s_stressBadgeLbls[i] = nullptr;
    }
}

static void clearAllPointers() {
    s_body = nullptr;
    s_hudCard = nullptr;
    s_fpsLbl = nullptr;
    s_heapLbl = nullptr;
    s_stressSw = nullptr;
    s_heroCard = nullptr;
    s_arcSpeed = nullptr;
    s_arcSpeedLbl = nullptr;
    s_arcLoad = nullptr;
    s_arcLoadLbl = nullptr;
    s_glassWrap = nullptr;
    s_glassOverlay = nullptr;
    s_glassTitle = nullptr;
    s_glassSub = nullptr;
    s_opaSlider = nullptr;
    s_chartCard = nullptr;
    s_chartTitle = nullptr;
    s_chartPeakLbl = nullptr;
    s_chart = nullptr;
    s_serA = nullptr;
    s_serB = nullptr;
    s_fontCard = nullptr;
    s_fontSpecLbl = nullptr;
    s_fontMetaLbl = nullptr;
    s_stressCard = nullptr;
    s_stressTitle = nullptr;
    clearStressPointers();
}

static uint32_t fpsColorHex(uint16_t fps) {
    if (fps >= 30) return M3Color::TERTIARY;
    if (fps >= 15) return M3Color::AMBER;
    return M3Color::CORAL;
}

static void updateHudLabels(uint16_t fps) {
    if (!s_fpsLbl || !s_heapLbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "FPS: %u", (unsigned)fps);
    lv_label_set_text(s_fpsLbl, buf);
    lv_obj_set_style_text_color(s_fpsLbl, lv_color_hex(fpsColorHex(fps)), 0);

    snprintf(buf, sizeof(buf), "Heap: %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
    lv_label_set_text(s_heapLbl, buf);
}

static void applyShadowIntensity() {
    const lv_coord_t shW = s_stressOn ? 16 : 8;
    const lv_opa_t shOpa = s_stressOn ? LV_OPA_60 : LV_OPA_30;
    lv_obj_t* cards[] = { s_hudCard, s_heroCard, s_glassOverlay, s_chartCard, s_fontCard, s_stressCard };
    for (lv_obj_t* c : cards) {
        if (!c) continue;
        lv_obj_set_style_shadow_width(c, shW, 0);
        lv_obj_set_style_shadow_opa(c, shOpa, 0);
        lv_obj_set_style_shadow_ofs_y(c, s_stressOn ? 5 : 3, 0);
    }
}

static void rebuildStressWidgets() {
    if (!s_stressCard) return;

    if (s_stressGrid) {
        lv_obj_del(s_stressGrid);
        clearStressPointers();
    }

    if (s_stressTitle) {
        lv_label_set_text(s_stressTitle,
            s_stressOn ? "Stress Arena (ON • 12 live widgets)" : "Stress Arena (OFF • tap switch)");
    }

    applyShadowIntensity();

    if (!s_stressOn) return;

    s_stressGrid = lv_obj_create(s_stressCard);
    lv_obj_set_size(s_stressGrid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_stressGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_stressGrid, 0, 0);
    lv_obj_set_style_pad_all(s_stressGrid, 0, 0);
    lv_obj_set_style_pad_row(s_stressGrid, 6, 0);
    lv_obj_set_style_pad_column(s_stressGrid, 6, 0);
    lv_obj_set_flex_flow(s_stressGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_stressGrid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_stressGrid, LV_OBJ_FLAG_SCROLLABLE);

    // 4 animated mini arcs
    static const uint32_t kArcColors[SQL_STRESS_ARCS] = { 0xFF6B9D, 0x78D9EC, 0xFDD663, 0x81C995 };
    for (uint8_t i = 0; i < SQL_STRESS_ARCS; ++i) {
        lv_obj_t* a = lv_arc_create(s_stressGrid);
        lv_obj_set_size(a, 44, 44);
        lv_arc_set_range(a, 0, 100);
        lv_arc_set_value(a, 25 + i * 15);
        lv_arc_set_bg_angles(a, 0, 360);
        lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(a, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, lv_color_hex(kArcColors[i]), LV_PART_INDICATOR);
        s_stressArcs[i] = a;
    }

    // 4 animated gradient bars with shadows
    static const uint32_t kBarStart[SQL_STRESS_BARS] = { 0xFF6B9D, 0x38BDF8, 0xA855F7, 0x34D399 };
    static const uint32_t kBarEnd[SQL_STRESS_BARS]   = { 0xF97316, 0x6366F1, 0xEC4899, 0x22D3EE };
    for (uint8_t i = 0; i < SQL_STRESS_BARS; ++i) {
        lv_obj_t* b = lv_bar_create(s_stressGrid);
        lv_obj_set_size(b, 92, 12);
        lv_bar_set_range(b, 0, 100);
        lv_bar_set_value(b, 40 + i * 12, LV_ANIM_OFF);
        lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 6, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(b, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_hex(kBarStart[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(b, lv_color_hex(kBarEnd[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(b, 8, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(b, lv_color_hex(kBarStart[i]), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(b, LV_OPA_40, LV_PART_INDICATOR);
        s_stressBars[i] = b;
    }

    // 4 translucent shadowed badges
    for (uint8_t i = 0; i < SQL_STRESS_BADGES; ++i) {
        lv_obj_t* tile = lv_obj_create(s_stressGrid);
        lv_obj_set_size(tile, 44, 30);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(kArcColors[i]), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_40, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(kArcColors[i]), 0);
        lv_obj_set_style_shadow_width(tile, 10, 0);
        lv_obj_set_style_shadow_color(tile, lv_color_hex(kArcColors[i]), 0);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(tile);
        lv_label_set_text_fmt(lbl, "S%u", (unsigned)(i + 1));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_center(lbl);

        s_stressBadges[i] = tile;
        s_stressBadgeLbls[i] = lbl;
    }
}

static void stressToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    s_stressOn = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    rebuildStressWidgets();
    updateHudLabels(s_lastFps);
}

static void opaSliderCb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    s_glassOpa = (uint8_t)v;
    if (s_glassOverlay) {
        lv_obj_set_style_bg_opa(s_glassOverlay, s_glassOpa, 0);
    }
    if (s_glassSub) {
        char buf[36];
        snprintf(buf, sizeof(buf), "Alpha: %u%% • Layered shadow", (unsigned)((s_glassOpa * 100) / 255));
        lv_label_set_text(s_glassSub, buf);
    }
}

static void cycleFontCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_fontIdx = (uint8_t)((s_fontIdx + 1) % kFontCount);
    if (s_fontSpecLbl) {
        lv_obj_set_style_text_font(s_fontSpecLbl, kFontLadder[s_fontIdx], 0);
    }
    if (s_fontMetaLbl) {
        char buf[36];
        snprintf(buf, sizeof(buf), "Montserrat %upx (tap to cycle)", (unsigned)kFontSizes[s_fontIdx]);
        lv_label_set_text(s_fontMetaLbl, buf);
    }
}

static void refreshAppSqLineTheme() {
    if (!s_body) return;
    bool light = g_sys.lightMode;

    if (s_hudCard) {
        lv_obj_set_style_bg_color(s_hudCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_bg_grad_color(s_hudCard, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_hudCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_heapLbl) {
        lv_obj_set_style_text_color(s_heapLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    }

    if (s_heroCard) {
        lv_obj_set_style_bg_color(s_heroCard, lv_color_hex(light ? 0xE8D8F8 : 0x2B1644), 0);
        lv_obj_set_style_bg_grad_color(s_heroCard, lv_color_hex(light ? 0xFAD4E4 : 0x13263E), 0);
        lv_obj_set_style_border_color(s_heroCard, lv_color_hex(light ? 0xD6A8E0 : 0x56327A), 0);
    }
    if (s_arcSpeed) {
        lv_obj_set_style_arc_color(s_arcSpeed, lv_color_hex(light ? 0xD8C6E8 : 0x2A2346), LV_PART_MAIN);
    }
    if (s_arcLoad) {
        lv_obj_set_style_arc_color(s_arcLoad, lv_color_hex(light ? 0xE8C6D6 : 0x36203A), LV_PART_MAIN);
    }
    if (s_arcSpeedLbl) lv_obj_set_style_text_color(s_arcSpeedLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_arcLoadLbl)  lv_obj_set_style_text_color(s_arcLoadLbl, lv_color_hex(m3TextPrimaryHex()), 0);

    if (s_glassOverlay) {
        lv_obj_set_style_bg_color(s_glassOverlay, lv_color_hex(light ? 0xFFFFFF : 0x161925), 0);
        lv_obj_set_style_border_color(s_glassOverlay, lv_color_hex(light ? 0xFFFFFF : 0x8AB4F8), 0);
    }
    if (s_glassTitle) lv_obj_set_style_text_color(s_glassTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_glassSub)   lv_obj_set_style_text_color(s_glassSub, lv_color_hex(m3TextSecondaryHex()), 0);

    if (s_chartCard) {
        lv_obj_set_style_bg_color(s_chartCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_chartCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_chartTitle) lv_obj_set_style_text_color(s_chartTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_chart) {
        lv_obj_set_style_bg_color(s_chart, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_line_color(s_chart, lv_color_hex(m3OutlineHex()), LV_PART_MAIN);
    }

    if (s_fontCard) {
        lv_obj_set_style_bg_color(s_fontCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_fontCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_fontSpecLbl) lv_obj_set_style_text_color(s_fontSpecLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_fontMetaLbl) lv_obj_set_style_text_color(s_fontMetaLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    if (s_stressCard) {
        lv_obj_set_style_bg_color(s_stressCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_stressCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_stressTitle) lv_obj_set_style_text_color(s_stressTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    for (uint8_t i = 0; i < SQL_STRESS_ARCS; ++i) {
        if (s_stressArcs[i]) {
            lv_obj_set_style_arc_color(s_stressArcs[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        }
    }
    for (uint8_t i = 0; i < SQL_STRESS_BARS; ++i) {
        if (s_stressBars[i]) {
            lv_obj_set_style_bg_color(s_stressBars[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        }
    }
    for (uint8_t i = 0; i < SQL_STRESS_BADGES; ++i) {
        if (s_stressBadgeLbls[i]) {
            lv_obj_set_style_text_color(s_stressBadgeLbls[i], lv_color_hex(m3TextPrimaryHex()), 0);
        }
    }
}

static void deleteCb(lv_event_t* e) {
    (void)e;
    s_stressOn = false;
    clearAllPointers();
}

static void buildAppSqLineLimits(lv_obj_t* body) {
    s_body = body;
    s_stressOn = false;
    s_fontIdx = 3;
    s_glassOpa = 155;
    s_phase = 0;
    s_lastAnimMs = 0;
    s_hudWindowStartMs = millis();
    s_hudFrames = 0;
    s_lastFps = 0;

    lv_obj_add_event_cb(body, deleteCb, LV_EVENT_DELETE, nullptr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    // 1. Live FPS & Free-Heap HUD + Stress Toggle
    s_hudCard = m3Card(body, LV_PCT(100), 42, 12);
    lv_obj_set_style_pad_hor(s_hudCard, 10, 0);
    lv_obj_set_style_pad_ver(s_hudCard, 4, 0);
    lv_obj_set_style_bg_grad_dir(s_hudCard, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(s_hudCard, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_hudCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_hudCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* hudLeft = lv_obj_create(s_hudCard);
    lv_obj_set_size(hudLeft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hudLeft, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hudLeft, 0, 0);
    lv_obj_set_style_pad_all(hudLeft, 0, 0);
    lv_obj_set_style_pad_row(hudLeft, 0, 0);
    lv_obj_set_flex_flow(hudLeft, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(hudLeft, LV_OBJ_FLAG_SCROLLABLE);

    s_fpsLbl = lv_label_create(hudLeft);
    lv_label_set_text(s_fpsLbl, "FPS: --");
    lv_obj_set_style_text_font(s_fpsLbl, &lv_font_montserrat_12, 0);

    s_heapLbl = lv_label_create(hudLeft);
    lv_label_set_text(s_heapLbl, "Heap: -- KB");
    lv_obj_set_style_text_font(s_heapLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* hudRight = lv_obj_create(s_hudCard);
    lv_obj_set_size(hudRight, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hudRight, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hudRight, 0, 0);
    lv_obj_set_style_pad_all(hudRight, 0, 0);
    lv_obj_set_style_pad_column(hudRight, 6, 0);
    lv_obj_set_flex_flow(hudRight, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hudRight, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hudRight, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* stLbl = lv_label_create(hudRight);
    lv_label_set_text(stLbl, "Stress");
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stLbl, lv_color_hex(0xFF6B9D), 0);

    s_stressSw = lv_switch_create(hudRight);
    lv_obj_set_size(s_stressSw, 38, 22);
    lv_obj_set_style_bg_color(s_stressSw, lv_color_hex(0xFF6B9D), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_stressSw, stressToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // 2. Hero Gradient + Dual Radial Arc Gauges
    s_heroCard = m3Card(body, LV_PCT(100), 112, 16);
    lv_obj_set_style_bg_grad_dir(s_heroCard, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(s_heroCard, lv_color_hex(0xFF6B9D), 0);
    lv_obj_set_style_pad_all(s_heroCard, 8, 0);
    lv_obj_set_flex_flow(s_heroCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_heroCard, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto makeGauge = [](lv_obj_t* parent, uint32_t colorHex, const char* unit, lv_obj_t** outArc, lv_obj_t** outVal) {
        lv_obj_t* arc = lv_arc_create(parent);
        lv_obj_set_size(arc, 88, 88);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 50);
        lv_arc_set_bg_angles(arc, 135, 45);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc, 9, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 9, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(colorHex), LV_PART_INDICATOR);

        lv_obj_t* val = lv_label_create(arc);
        lv_label_set_text(val, "50");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
        lv_obj_align(val, LV_ALIGN_CENTER, 0, -4);

        lv_obj_t* u = lv_label_create(arc);
        lv_label_set_text(u, unit);
        lv_obj_set_style_text_font(u, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(u, lv_color_hex(colorHex), 0);
        lv_obj_align(u, LV_ALIGN_CENTER, 0, 14);

        *outArc = arc;
        *outVal = val;
    };

    makeGauge(s_heroCard, 0x38BDF8, "RPM %", &s_arcSpeed, &s_arcSpeedLbl);
    makeGauge(s_heroCard, 0xFF6B9D, "GPU %", &s_arcLoad, &s_arcLoadLbl);

    // 3. Translucent Glassmorphic Card over Gradient Backdrop + Alpha Slider
    s_glassWrap = lv_obj_create(body);
    lv_obj_set_size(s_glassWrap, LV_PCT(100), 84);
    lv_obj_set_style_radius(s_glassWrap, 16, 0);
    lv_obj_set_style_bg_color(s_glassWrap, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_style_bg_grad_color(s_glassWrap, lv_color_hex(0xEC4899), 0);
    lv_obj_set_style_bg_grad_dir(s_glassWrap, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(s_glassWrap, 0, 0);
    lv_obj_set_style_pad_all(s_glassWrap, 8, 0);
    lv_obj_clear_flag(s_glassWrap, LV_OBJ_FLAG_SCROLLABLE);

    // Decorative backdrop orbs to reveal translucency clearly
    lv_obj_t* orb1 = lv_obj_create(s_glassWrap);
    lv_obj_set_size(orb1, 46, 46);
    lv_obj_set_pos(orb1, 6, 12);
    lv_obj_set_style_radius(orb1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb1, lv_color_hex(0xFDD663), 0);
    lv_obj_set_style_border_width(orb1, 0, 0);
    lv_obj_clear_flag(orb1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* orb2 = lv_obj_create(s_glassWrap);
    lv_obj_set_size(orb2, 52, 52);
    lv_obj_set_pos(orb2, 146, 8);
    lv_obj_set_style_radius(orb2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb2, lv_color_hex(0x34D399), 0);
    lv_obj_set_style_border_width(orb2, 0, 0);
    lv_obj_clear_flag(orb2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_glassOverlay = lv_obj_create(s_glassWrap);
    lv_obj_set_size(s_glassOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(s_glassOverlay, 12, 0);
    lv_obj_set_style_bg_opa(s_glassOverlay, s_glassOpa, 0);
    lv_obj_set_style_border_width(s_glassOverlay, 1, 0);
    lv_obj_set_style_border_opa(s_glassOverlay, LV_OPA_50, 0);
    lv_obj_set_style_shadow_color(s_glassOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(s_glassOverlay, 10, 0);
    lv_obj_set_style_pad_ver(s_glassOverlay, 6, 0);
    lv_obj_set_flex_flow(s_glassOverlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_glassOverlay, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_glassOverlay, LV_OBJ_FLAG_SCROLLABLE);

    s_glassTitle = lv_label_create(s_glassOverlay);
    lv_label_set_text(s_glassTitle, "Translucent Glass Card");
    lv_obj_set_style_text_font(s_glassTitle, &lv_font_montserrat_12, 0);

    s_glassSub = lv_label_create(s_glassOverlay);
    lv_label_set_text(s_glassSub, "Alpha: 60% • Layered shadow");
    lv_obj_set_style_text_font(s_glassSub, &lv_font_montserrat_10, 0);

    s_opaSlider = lv_slider_create(s_glassOverlay);
    lv_obj_set_size(s_opaSlider, LV_PCT(100), 8);
    lv_slider_set_range(s_opaSlider, 50, 235);
    lv_slider_set_value(s_opaSlider, s_glassOpa, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_opaSlider, lv_color_hex(0xFF6B9D), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_opaSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_opaSlider, 3, LV_PART_KNOB);
    lv_obj_add_event_cb(s_opaSlider, opaSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // 4. Animated Dual-Series Telemetry Chart
    s_chartCard = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(s_chartCard, 8, 0);
    lv_obj_set_style_pad_row(s_chartCard, 6, 0);
    lv_obj_set_style_shadow_color(s_chartCard, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_chartCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* chartHdr = lv_obj_create(s_chartCard);
    lv_obj_set_size(chartHdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(chartHdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chartHdr, 0, 0);
    lv_obj_set_style_pad_all(chartHdr, 0, 0);
    lv_obj_set_flex_flow(chartHdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chartHdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chartHdr, LV_OBJ_FLAG_SCROLLABLE);

    s_chartTitle = lv_label_create(chartHdr);
    lv_label_set_text(s_chartTitle, "Live Waveform");
    lv_obj_set_style_text_font(s_chartTitle, &lv_font_montserrat_12, 0);

    s_chartPeakLbl = lv_label_create(chartHdr);
    lv_label_set_text(s_chartPeakLbl, "Pk: --");
    lv_obj_set_style_text_font(s_chartPeakLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chartPeakLbl, lv_color_hex(0x38BDF8), 0);

    s_chart = lv_chart_create(s_chartCard);
    lv_obj_set_size(s_chart, LV_PCT(100), 68);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, SQL_CHART_PTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_chart, 3, 4);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_radius(s_chart, 8, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR); // clean line without point dots
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    s_serA = lv_chart_add_series(s_chart, lv_color_hex(0x38BDF8), LV_CHART_AXIS_PRIMARY_Y);
    s_serB = lv_chart_add_series(s_chart, lv_color_hex(0xFF6B9D), LV_CHART_AXIS_PRIMARY_Y);
    for (uint8_t i = 0; i < SQL_CHART_PTS; ++i) {
        lv_chart_set_next_value(s_chart, s_serA, 50 + (lv_coord_t)(30.0f * sinf(i * 0.45f)));
        lv_chart_set_next_value(s_chart, s_serB, 50 + (lv_coord_t)(24.0f * cosf(i * 0.35f)));
    }

    // 5. Interactive Font Scaling Showcase
    s_fontCard = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(s_fontCard, 10, 0);
    lv_obj_set_style_pad_row(s_fontCard, 4, 0);
    lv_obj_set_style_shadow_color(s_fontCard, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_fontCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* fontHdr = lv_obj_create(s_fontCard);
    lv_obj_set_size(fontHdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fontHdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fontHdr, 0, 0);
    lv_obj_set_style_pad_all(fontHdr, 0, 0);
    lv_obj_set_flex_flow(fontHdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fontHdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(fontHdr, LV_OBJ_FLAG_SCROLLABLE);

    s_fontMetaLbl = lv_label_create(fontHdr);
    lv_label_set_text(s_fontMetaLbl, "Montserrat 24px (tap to cycle)");
    lv_obj_set_style_text_font(s_fontMetaLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* scaleBtn = m3PillBtn(fontHdr, "Scale", 0xFF6B9D, 0x1A0B14, 52, 22);
    lv_obj_add_event_cb(scaleBtn, cycleFontCb, LV_EVENT_CLICKED, nullptr);

    s_fontSpecLbl = lv_label_create(s_fontCard);
    lv_label_set_text(s_fontSpecLbl, "SqLine 98.6%");
    lv_obj_set_style_text_font(s_fontSpecLbl, kFontLadder[s_fontIdx], 0);

    // 6. Widget Stress Arena Container
    s_stressCard = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(s_stressCard, 8, 0);
    lv_obj_set_style_pad_row(s_stressCard, 8, 0);
    lv_obj_set_style_shadow_color(s_stressCard, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_stressCard, LV_FLEX_FLOW_COLUMN);

    s_stressTitle = lv_label_create(s_stressCard);
    lv_label_set_text(s_stressTitle, "Stress Arena (OFF • tap switch)");
    lv_obj_set_style_text_font(s_stressTitle, &lv_font_montserrat_12, 0);

    applyShadowIntensity();
    refreshAppSqLineTheme();
    updateHudLabels(0);
}

static void tickAppSqLineLimits() {
    if (!s_body) return;

    uint32_t now = millis();
    ++s_hudFrames;
    uint32_t hudElapsed = now - s_hudWindowStartMs;
    if (hudElapsed >= SQL_HUD_WINDOW_MS) {
        s_lastFps = (uint16_t)((s_hudFrames * 1000UL) / hudElapsed);
        s_hudFrames = 0;
        s_hudWindowStartMs = now;
        updateHudLabels(s_lastFps);
    }

    if (now - s_lastAnimMs < SQL_ANIM_INTERVAL_MS) return;
    s_lastAnimMs = now;
    ++s_phase;

    float t = s_phase * 0.14f;
    int32_t speed = (int32_t)(50.0f + 44.0f * sinf(t));
    int32_t load  = (int32_t)(50.0f + 40.0f * cosf(t * 0.73f + 0.8f));

    if (s_arcSpeed) lv_arc_set_value(s_arcSpeed, speed);
    if (s_arcSpeedLbl) lv_label_set_text_fmt(s_arcSpeedLbl, "%ld", (long)speed);
    if (s_arcLoad) lv_arc_set_value(s_arcLoad, load);
    if (s_arcLoadLbl) lv_label_set_text_fmt(s_arcLoadLbl, "%ld", (long)load);

    if (s_chart && s_serA && s_serB) {
        lv_chart_set_next_value(s_chart, s_serA, (lv_coord_t)speed);
        lv_chart_set_next_value(s_chart, s_serB, (lv_coord_t)load);
        if (s_chartPeakLbl) {
            int32_t pk = (speed > load) ? speed : load;
            lv_label_set_text_fmt(s_chartPeakLbl, "Pk: %ld%%", (long)pk);
        }
    }

    if (s_stressOn) {
        for (uint8_t i = 0; i < SQL_STRESS_ARCS; ++i) {
            if (s_stressArcs[i]) {
                int32_t v = (int32_t)(50.0f + 45.0f * sinf(t * (1.0f + 0.25f * i) + i * 1.1f));
                lv_arc_set_value(s_stressArcs[i], v);
            }
        }
        for (uint8_t i = 0; i < SQL_STRESS_BARS; ++i) {
            if (s_stressBars[i]) {
                int32_t v = (int32_t)(50.0f + 45.0f * cosf(t * (0.9f + 0.3f * i) + i * 0.7f));
                lv_bar_set_value(s_stressBars[i], v, LV_ANIM_OFF);
            }
        }
        for (uint8_t i = 0; i < SQL_STRESS_BADGES; ++i) {
            if (s_stressBadges[i]) {
                lv_opa_t opa = (lv_opa_t)(85 + (uint8_t)(70.0f * (0.5f + 0.5f * sinf(t * 1.4f + i))));
                lv_obj_set_style_bg_opa(s_stressBadges[i], opa, 0);
            }
            if (s_stressBadgeLbls[i]) {
                lv_label_set_text_fmt(s_stressBadgeLbls[i], "%02u", (unsigned)((s_phase + i * 17) % 100));
            }
        }
    }
}

} // namespace

REGISTER_APP_THEMED(
    sqline_limits,
    "sqline_limits",
    "SqLine Lab",
    LV_SYMBOL_EYE_OPEN,
    0xFF6B9D,
    0xDB2777,
    buildAppSqLineLimits,
    tickAppSqLineLimits,
    refreshAppSqLineTheme,
    150
);
