#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

namespace {

constexpr uint8_t EEZ_CHART_PTS = 10;
constexpr uint8_t EEZ_CH_COUNT = 3;
constexpr uint8_t EEZ_STRESS_DIALS = 4;
constexpr uint8_t EEZ_STRESS_BARS = 4;
constexpr uint8_t EEZ_STRESS_TILES = 4;
constexpr uint8_t EEZ_STRESS_SPINNERS = 4;
constexpr uint32_t EEZ_HUD_WINDOW_MS = 500;
constexpr uint32_t EEZ_ANIM_INTERVAL_MS = 40; // ~25 Hz instrument step

static const lv_font_t* const kFontLadder[] = {
    &lv_font_montserrat_10,
    &lv_font_montserrat_12,
    &lv_font_montserrat_14,
    &lv_font_montserrat_16,
    &lv_font_montserrat_18,
    &lv_font_montserrat_20,
    &lv_font_montserrat_24,
    &lv_font_montserrat_28,
    &lv_font_montserrat_36,
};
static const uint8_t kFontSizes[] = { 10, 12, 14, 16, 18, 20, 24, 28, 36 };
constexpr uint8_t kFontCount = sizeof(kFontLadder) / sizeof(kFontLadder[0]);

// Persistent UI pointers (nulled in LV_EVENT_DELETE)
static lv_obj_t* s_body = nullptr;
static lv_obj_t* s_hudCard = nullptr;
static lv_obj_t* s_fpsLbl = nullptr;
static lv_obj_t* s_heapLbl = nullptr;
static lv_obj_t* s_stressSw = nullptr;

static lv_obj_t* s_instCard = nullptr;
static lv_obj_t* s_pwrArc = nullptr;
static lv_obj_t* s_pwrValLbl = nullptr;
static lv_obj_t* s_chValLbls[EEZ_CH_COUNT] = {};
static lv_obj_t* s_chBars[EEZ_CH_COUNT] = {};

static lv_obj_t* s_chartCard = nullptr;
static lv_obj_t* s_chartTitle = nullptr;
static lv_obj_t* s_chartModeBtn = nullptr;
static lv_obj_t* s_chartModeLbl = nullptr;
static lv_obj_t* s_chart = nullptr;
static lv_chart_series_t* s_serCh1 = nullptr;
static lv_chart_series_t* s_serCh2 = nullptr;

static lv_obj_t* s_layerWrap = nullptr;
static lv_obj_t* s_layerCards[3] = {};
static lv_obj_t* s_layerLbls[3] = {};

static lv_obj_t* s_fontCard = nullptr;
static lv_obj_t* s_fontSizeLbl = nullptr;
static lv_obj_t* s_fontSampleLbl = nullptr;

static lv_obj_t* s_stressCard = nullptr;
static lv_obj_t* s_stressTitle = nullptr;
static lv_obj_t* s_stressGrid = nullptr;
static lv_obj_t* s_stressDials[EEZ_STRESS_DIALS] = {};
static lv_obj_t* s_stressBars[EEZ_STRESS_BARS] = {};
static lv_obj_t* s_stressTiles[EEZ_STRESS_TILES] = {};
static lv_obj_t* s_stressTileLbls[EEZ_STRESS_TILES] = {};
static lv_obj_t* s_stressSpinners[EEZ_STRESS_SPINNERS] = {};

// Runtime state
static bool s_stressOn = false;
static bool s_barMode = true; // true = BAR, false = LINE
static uint8_t s_fontIdx = 5; // starts at 20px
static uint32_t s_phase = 0;
static uint32_t s_lastAnimMs = 0;
static uint32_t s_hudWindowStartMs = 0;
static uint32_t s_hudFrames = 0;
static uint16_t s_lastFps = 0;

static void clearStressPointers() {
    s_stressGrid = nullptr;
    for (uint8_t i = 0; i < EEZ_STRESS_DIALS; ++i) s_stressDials[i] = nullptr;
    for (uint8_t i = 0; i < EEZ_STRESS_BARS; ++i) s_stressBars[i] = nullptr;
    for (uint8_t i = 0; i < EEZ_STRESS_TILES; ++i) {
        s_stressTiles[i] = nullptr;
        s_stressTileLbls[i] = nullptr;
    }
    for (uint8_t i = 0; i < EEZ_STRESS_SPINNERS; ++i) s_stressSpinners[i] = nullptr;
}

static void clearAllPointers() {
    s_body = nullptr;
    s_hudCard = nullptr;
    s_fpsLbl = nullptr;
    s_heapLbl = nullptr;
    s_stressSw = nullptr;
    s_instCard = nullptr;
    s_pwrArc = nullptr;
    s_pwrValLbl = nullptr;
    for (uint8_t i = 0; i < EEZ_CH_COUNT; ++i) {
        s_chValLbls[i] = nullptr;
        s_chBars[i] = nullptr;
    }
    s_chartCard = nullptr;
    s_chartTitle = nullptr;
    s_chartModeBtn = nullptr;
    s_chartModeLbl = nullptr;
    s_chart = nullptr;
    s_serCh1 = nullptr;
    s_serCh2 = nullptr;
    s_layerWrap = nullptr;
    for (uint8_t i = 0; i < 3; ++i) {
        s_layerCards[i] = nullptr;
        s_layerLbls[i] = nullptr;
    }
    s_fontCard = nullptr;
    s_fontSizeLbl = nullptr;
    s_fontSampleLbl = nullptr;
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
    char buf[36];
    snprintf(buf, sizeof(buf), "FPS: %u", (unsigned)fps);
    lv_label_set_text(s_fpsLbl, buf);
    lv_obj_set_style_text_color(s_fpsLbl, lv_color_hex(fpsColorHex(fps)), 0);

    snprintf(buf, sizeof(buf), "Heap: %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
    lv_label_set_text(s_heapLbl, buf);
}

static void applyShadowIntensity() {
    const lv_coord_t shW = s_stressOn ? 16 : 6;
    const lv_opa_t shOpa = s_stressOn ? LV_OPA_60 : LV_OPA_30;
    lv_obj_t* cards[] = { s_hudCard, s_instCard, s_chartCard, s_fontCard, s_stressCard };
    for (lv_obj_t* c : cards) {
        if (!c) continue;
        lv_obj_set_style_shadow_width(c, shW, 0);
        lv_obj_set_style_shadow_opa(c, shOpa, 0);
        lv_obj_set_style_shadow_ofs_y(c, s_stressOn ? 4 : 2, 0);
    }
}

static void rebuildStressMatrix() {
    if (!s_stressCard) return;

    if (s_stressGrid) {
        lv_obj_del(s_stressGrid);
        clearStressPointers();
    }

    if (s_stressTitle) {
        lv_label_set_text(s_stressTitle,
            s_stressOn ? "HMI Matrix (ON • 16 live widgets)" : "HMI Matrix (OFF • tap switch)");
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

    static const uint32_t kColors[4] = { 0x38BDF8, 0x34D399, 0xFBBF24, 0xA78BFA };

    // Row 1: 4 micro dials
    for (uint8_t i = 0; i < EEZ_STRESS_DIALS; ++i) {
        lv_obj_t* d = lv_arc_create(s_stressGrid);
        lv_obj_set_size(d, 44, 44);
        lv_arc_set_range(d, 0, 100);
        lv_arc_set_value(d, 30 + i * 18);
        lv_arc_set_bg_angles(d, 135, 45);
        lv_obj_remove_style(d, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(d, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(d, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(d, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        lv_obj_set_style_arc_color(d, lv_color_hex(kColors[i]), LV_PART_INDICATOR);
        s_stressDials[i] = d;
    }

    // Row 2: 4 animated level meters
    for (uint8_t i = 0; i < EEZ_STRESS_BARS; ++i) {
        lv_obj_t* b = lv_bar_create(s_stressGrid);
        lv_obj_set_size(b, 92, 10);
        lv_bar_set_range(b, 0, 100);
        lv_bar_set_value(b, 50, LV_ANIM_OFF);
        lv_obj_set_style_radius(b, 5, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 5, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(b, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_hex(kColors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(b, lv_color_hex(0x0EA5E9), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        s_stressBars[i] = b;
    }

    // Row 3: 4 shadowed translucent flow tiles
    for (uint8_t i = 0; i < EEZ_STRESS_TILES; ++i) {
        lv_obj_t* t = lv_obj_create(s_stressGrid);
        lv_obj_set_size(t, 44, 28);
        lv_obj_set_style_radius(t, 6, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(kColors[i]), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_30, 0);
        lv_obj_set_style_border_width(t, 1, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(kColors[i]), 0);
        lv_obj_set_style_shadow_width(t, 10, 0);
        lv_obj_set_style_shadow_color(t, lv_color_hex(kColors[i]), 0);
        lv_obj_set_style_shadow_opa(t, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(t);
        lv_label_set_text_fmt(lbl, "F%u", (unsigned)(i + 1));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_center(lbl);

        s_stressTiles[i] = t;
        s_stressTileLbls[i] = lbl;
    }

    // Row 4: 4 live LVGL spinners
    for (uint8_t i = 0; i < EEZ_STRESS_SPINNERS; ++i) {
        lv_obj_t* sp = lv_spinner_create(s_stressGrid, 900 + i * 220, 75);
        lv_obj_set_size(sp, 34, 34);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(sp, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sp, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, lv_color_hex(kColors[i]), LV_PART_INDICATOR);
        s_stressSpinners[i] = sp;
    }
}

static void stressToggleCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    s_stressOn = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    rebuildStressMatrix();
    updateHudLabels(s_lastFps);
}

static void chartModeCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_barMode = !s_barMode;
    if (s_chart) {
        lv_chart_set_type(s_chart, s_barMode ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
        lv_chart_refresh(s_chart);
    }
    if (s_chartModeLbl) {
        lv_label_set_text(s_chartModeLbl, s_barMode ? "Bars" : "Line");
    }
}

static void updateFontCard() {
    if (s_fontSampleLbl) {
        lv_obj_set_style_text_font(s_fontSampleLbl, kFontLadder[s_fontIdx], 0);
    }
    if (s_fontSizeLbl) {
        lv_label_set_text_fmt(s_fontSizeLbl, "Scale: %upx", (unsigned)kFontSizes[s_fontIdx]);
    }
}

static void fontDecCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (s_fontIdx > 0) --s_fontIdx;
    updateFontCard();
}

static void fontIncCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (s_fontIdx + 1 < kFontCount) ++s_fontIdx;
    updateFontCard();
}

static void refreshAppEezTheme() {
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

    if (s_instCard) {
        lv_obj_set_style_bg_color(s_instCard, lv_color_hex(light ? 0xDCEEFB : 0x102235), 0);
        lv_obj_set_style_bg_grad_color(s_instCard, lv_color_hex(light ? 0xE6F7F0 : 0x122A28), 0);
        lv_obj_set_style_border_color(s_instCard, lv_color_hex(light ? 0x9BCBE8 : 0x265273), 0);
    }
    if (s_pwrArc) {
        lv_obj_set_style_arc_color(s_pwrArc, lv_color_hex(light ? 0xC2DFF0 : 0x1C3852), LV_PART_MAIN);
    }
    if (s_pwrValLbl) lv_obj_set_style_text_color(s_pwrValLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    for (uint8_t i = 0; i < EEZ_CH_COUNT; ++i) {
        if (s_chValLbls[i]) lv_obj_set_style_text_color(s_chValLbls[i], lv_color_hex(m3TextPrimaryHex()), 0);
        if (s_chBars[i]) lv_obj_set_style_bg_color(s_chBars[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    }

    if (s_chartCard) {
        lv_obj_set_style_bg_color(s_chartCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_chartCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_chartTitle) lv_obj_set_style_text_color(s_chartTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    if (s_chart) {
        lv_obj_set_style_bg_color(s_chart, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_line_color(s_chart, lv_color_hex(m3OutlineHex()), LV_PART_MAIN);
    }

    for (uint8_t i = 0; i < 3; ++i) {
        if (s_layerCards[i]) {
            lv_obj_set_style_bg_color(s_layerCards[i], lv_color_hex(light ? 0xFFFFFF : 0x0F172A), 0);
        }
        if (s_layerLbls[i]) {
            lv_obj_set_style_text_color(s_layerLbls[i], lv_color_hex(m3TextPrimaryHex()), 0);
        }
    }

    if (s_fontCard) {
        lv_obj_set_style_bg_color(s_fontCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_fontCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_fontSizeLbl)   lv_obj_set_style_text_color(s_fontSizeLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    if (s_fontSampleLbl) lv_obj_set_style_text_color(s_fontSampleLbl, lv_color_hex(m3TextPrimaryHex()), 0);

    if (s_stressCard) {
        lv_obj_set_style_bg_color(s_stressCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_stressCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_stressTitle) lv_obj_set_style_text_color(s_stressTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    for (uint8_t i = 0; i < EEZ_STRESS_DIALS; ++i) {
        if (s_stressDials[i]) lv_obj_set_style_arc_color(s_stressDials[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    }
    for (uint8_t i = 0; i < EEZ_STRESS_BARS; ++i) {
        if (s_stressBars[i]) lv_obj_set_style_bg_color(s_stressBars[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    }
    for (uint8_t i = 0; i < EEZ_STRESS_TILES; ++i) {
        if (s_stressTileLbls[i]) lv_obj_set_style_text_color(s_stressTileLbls[i], lv_color_hex(m3TextPrimaryHex()), 0);
    }
    for (uint8_t i = 0; i < EEZ_STRESS_SPINNERS; ++i) {
        if (s_stressSpinners[i]) lv_obj_set_style_arc_color(s_stressSpinners[i], lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    }
}

static void deleteCb(lv_event_t* e) {
    (void)e;
    s_stressOn = false;
    clearAllPointers();
}

static void buildAppEezLimits(lv_obj_t* body) {
    s_body = body;
    s_stressOn = false;
    s_barMode = true;
    s_fontIdx = 5;
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
    lv_obj_set_style_text_color(stLbl, lv_color_hex(0x38BDF8), 0);

    s_stressSw = lv_switch_create(hudRight);
    lv_obj_set_size(s_stressSw, 38, 22);
    lv_obj_set_style_bg_color(s_stressSw, lv_color_hex(0x38BDF8), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_stressSw, stressToggleCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // 2. Precision Instrument Gauge + Multi-Channel Bars
    s_instCard = m3Card(body, LV_PCT(100), 108, 16);
    lv_obj_set_style_bg_grad_dir(s_instCard, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(s_instCard, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_pad_all(s_instCard, 8, 0);
    lv_obj_set_style_pad_column(s_instCard, 8, 0);
    lv_obj_set_flex_flow(s_instCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_instCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_pwrArc = lv_arc_create(s_instCard);
    lv_obj_set_size(s_pwrArc, 86, 86);
    lv_arc_set_range(s_pwrArc, 0, 120);
    lv_arc_set_value(s_pwrArc, 64);
    lv_arc_set_bg_angles(s_pwrArc, 135, 45);
    lv_obj_remove_style(s_pwrArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(s_pwrArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_pwrArc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_pwrArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_pwrArc, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);

    s_pwrValLbl = lv_label_create(s_pwrArc);
    lv_label_set_text(s_pwrValLbl, "64");
    lv_obj_set_style_text_font(s_pwrValLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(s_pwrValLbl, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t* pwrUnit = lv_label_create(s_pwrArc);
    lv_label_set_text(pwrUnit, "WATTS");
    lv_obj_set_style_text_font(pwrUnit, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(pwrUnit, lv_color_hex(0x38BDF8), 0);
    lv_obj_align(pwrUnit, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t* chCol = lv_obj_create(s_instCard);
    lv_obj_set_size(chCol, 108, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(chCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chCol, 0, 0);
    lv_obj_set_style_pad_all(chCol, 0, 0);
    lv_obj_set_style_pad_row(chCol, 5, 0);
    lv_obj_set_flex_flow(chCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(chCol, LV_OBJ_FLAG_SCROLLABLE);

    static const uint32_t kChColors[EEZ_CH_COUNT] = { 0x38BDF8, 0x34D399, 0xFBBF24 };
    static const char* const kChInit[EEZ_CH_COUNT] = { "CH1 12.0V", "CH2 3.30A", "EFF 92%" };
    for (uint8_t i = 0; i < EEZ_CH_COUNT; ++i) {
        s_chValLbls[i] = lv_label_create(chCol);
        lv_label_set_text(s_chValLbls[i], kChInit[i]);
        lv_obj_set_style_text_font(s_chValLbls[i], &lv_font_montserrat_10, 0);

        s_chBars[i] = lv_bar_create(chCol);
        lv_obj_set_size(s_chBars[i], LV_PCT(100), 6);
        lv_bar_set_range(s_chBars[i], 0, 100);
        lv_bar_set_value(s_chBars[i], 60 + i * 10, LV_ANIM_OFF);
        lv_obj_set_style_radius(s_chBars[i], 3, LV_PART_MAIN);
        lv_obj_set_style_radius(s_chBars[i], 3, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_chBars[i], lv_color_hex(kChColors[i]), LV_PART_INDICATOR);
    }

    // 3. Animated Multi-Channel Spectrum Chart (Bars / Line toggle)
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
    lv_label_set_text(s_chartTitle, "Harmonic Spectrum");
    lv_obj_set_style_text_font(s_chartTitle, &lv_font_montserrat_12, 0);

    s_chartModeBtn = m3PillBtn(chartHdr, "Bars", 0x38BDF8, 0x061926, 48, 20);
    s_chartModeLbl = lv_obj_get_child(s_chartModeBtn, 0);
    lv_obj_add_event_cb(s_chartModeBtn, chartModeCb, LV_EVENT_CLICKED, nullptr);

    s_chart = lv_chart_create(s_chartCard);
    lv_obj_set_size(s_chart, LV_PCT(100), 68);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_chart, EEZ_CHART_PTS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_chart, 3, 5);
    lv_obj_set_style_radius(s_chart, 8, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_column(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
    s_serCh1 = lv_chart_add_series(s_chart, lv_color_hex(0x38BDF8), LV_CHART_AXIS_PRIMARY_Y);
    s_serCh2 = lv_chart_add_series(s_chart, lv_color_hex(0x34D399), LV_CHART_AXIS_PRIMARY_Y);
    for (uint8_t i = 0; i < EEZ_CHART_PTS; ++i) {
        lv_chart_set_value_by_id(s_chart, s_serCh1, i, 30 + (i * 7) % 60);
        lv_chart_set_value_by_id(s_chart, s_serCh2, i, 20 + (i * 11) % 65);
    }

    // 4. Translucent Alpha & Progressive Shadow Depth Deck
    s_layerWrap = lv_obj_create(body);
    lv_obj_set_size(s_layerWrap, LV_PCT(100), 72);
    lv_obj_set_style_radius(s_layerWrap, 14, 0);
    lv_obj_set_style_bg_color(s_layerWrap, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_bg_grad_color(s_layerWrap, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_bg_grad_dir(s_layerWrap, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(s_layerWrap, 0, 0);
    lv_obj_set_style_pad_all(s_layerWrap, 8, 0);
    lv_obj_set_style_pad_column(s_layerWrap, 6, 0);
    lv_obj_set_flex_flow(s_layerWrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_layerWrap, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_layerWrap, LV_OBJ_FLAG_SCROLLABLE);

    static const lv_opa_t kLayerOpas[3] = { LV_OPA_40, LV_OPA_60, LV_OPA_80 };
    static const lv_coord_t kLayerShadows[3] = { 4, 10, 16 };
    static const char* const kLayerTxt[3] = { "40%\nS4", "60%\nS10", "80%\nS16" };
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t* c = lv_obj_create(s_layerWrap);
        lv_obj_set_size(c, 60, 52);
        lv_obj_set_style_radius(c, 10, 0);
        lv_obj_set_style_bg_opa(c, kLayerOpas[i], 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(c, LV_OPA_50, 0);
        lv_obj_set_style_shadow_width(c, kLayerShadows[i], 0);
        lv_obj_set_style_shadow_color(c, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(c, LV_OPA_50, 0);
        lv_obj_set_style_shadow_ofs_y(c, 2 + i * 2, 0);
        lv_obj_set_style_pad_all(c, 2, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(c);
        lv_label_set_text(lbl, kLayerTxt[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        s_layerCards[i] = c;
        s_layerLbls[i] = lbl;
    }

    // 5. Dynamic Font Scaling Controls (A- / A+)
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

    s_fontSizeLbl = lv_label_create(fontHdr);
    lv_label_set_text(s_fontSizeLbl, "Scale: 20px");
    lv_obj_set_style_text_font(s_fontSizeLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* btnRow = lv_obj_create(fontHdr);
    lv_obj_set_size(btnRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 6, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* decBtn = m3PillBtn(btnRow, "A-", 0x22384D, 0x38BDF8, 34, 22);
    lv_obj_add_event_cb(decBtn, fontDecCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* incBtn = m3PillBtn(btnRow, "A+", 0x38BDF8, 0x061926, 34, 22);
    lv_obj_add_event_cb(incBtn, fontIncCb, LV_EVENT_CLICKED, nullptr);

    s_fontSampleLbl = lv_label_create(s_fontCard);
    lv_label_set_text(s_fontSampleLbl, "EEZ 24.50V");
    lv_obj_set_style_text_font(s_fontSampleLbl, kFontLadder[s_fontIdx], 0);

    // 6. HMI Widget Stress Matrix Container
    s_stressCard = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(s_stressCard, 8, 0);
    lv_obj_set_style_pad_row(s_stressCard, 8, 0);
    lv_obj_set_style_shadow_color(s_stressCard, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_stressCard, LV_FLEX_FLOW_COLUMN);

    s_stressTitle = lv_label_create(s_stressCard);
    lv_label_set_text(s_stressTitle, "HMI Matrix (OFF • tap switch)");
    lv_obj_set_style_text_font(s_stressTitle, &lv_font_montserrat_12, 0);

    applyShadowIntensity();
    refreshAppEezTheme();
    updateHudLabels(0);
}

static void tickAppEezLimits() {
    if (!s_body) return;

    uint32_t now = millis();
    ++s_hudFrames;
    uint32_t hudElapsed = now - s_hudWindowStartMs;
    if (hudElapsed >= EEZ_HUD_WINDOW_MS) {
        s_lastFps = (uint16_t)((s_hudFrames * 1000UL) / hudElapsed);
        s_hudFrames = 0;
        s_hudWindowStartMs = now;
        updateHudLabels(s_lastFps);
    }

    if (now - s_lastAnimMs < EEZ_ANIM_INTERVAL_MS) return;
    s_lastAnimMs = now;
    ++s_phase;

    float t = s_phase * 0.16f;
    int32_t watts = (int32_t)(60.0f + 50.0f * sinf(t));
    if (s_pwrArc) lv_arc_set_value(s_pwrArc, watts);
    if (s_pwrValLbl) lv_label_set_text_fmt(s_pwrValLbl, "%ld", (long)watts);

    float v1 = 12.0f + 2.4f * sinf(t * 0.8f);
    float a2 = 2.8f + 1.5f * cosf(t * 1.1f);
    int32_t eff = (int32_t)(88.0f + 9.0f * sinf(t * 0.5f));
    char buf[24];
    if (s_chValLbls[0]) {
        snprintf(buf, sizeof(buf), "CH1 %0.1fV", (double)v1);
        lv_label_set_text(s_chValLbls[0], buf);
    }
    if (s_chBars[0]) lv_bar_set_value(s_chBars[0], (int32_t)(v1 * 6.0f), LV_ANIM_OFF);

    if (s_chValLbls[1]) {
        snprintf(buf, sizeof(buf), "CH2 %0.2fA", (double)a2);
        lv_label_set_text(s_chValLbls[1], buf);
    }
    if (s_chBars[1]) lv_bar_set_value(s_chBars[1], (int32_t)(a2 * 20.0f), LV_ANIM_OFF);

    if (s_chValLbls[2]) {
        snprintf(buf, sizeof(buf), "EFF %ld%%", (long)eff);
        lv_label_set_text(s_chValLbls[2], buf);
    }
    if (s_chBars[2]) lv_bar_set_value(s_chBars[2], eff, LV_ANIM_OFF);

    if (s_chart && s_serCh1 && s_serCh2) {
        for (uint8_t i = 0; i < EEZ_CHART_PTS; ++i) {
            lv_coord_t c1 = (lv_coord_t)(50.0f + 42.0f * sinf(t + i * 0.65f));
            lv_coord_t c2 = (lv_coord_t)(48.0f + 38.0f * cosf(t * 1.25f + i * 0.55f));
            lv_chart_set_value_by_id(s_chart, s_serCh1, i, c1);
            lv_chart_set_value_by_id(s_chart, s_serCh2, i, c2);
        }
        lv_chart_refresh(s_chart);
    }

    if (s_stressOn) {
        for (uint8_t i = 0; i < EEZ_STRESS_DIALS; ++i) {
            if (s_stressDials[i]) {
                int32_t v = (int32_t)(50.0f + 46.0f * sinf(t * (1.1f + 0.2f * i) + i * 0.9f));
                lv_arc_set_value(s_stressDials[i], v);
            }
        }
        for (uint8_t i = 0; i < EEZ_STRESS_BARS; ++i) {
            if (s_stressBars[i]) {
                int32_t v = (int32_t)(50.0f + 46.0f * cosf(t * (0.85f + 0.25f * i) + i));
                lv_bar_set_value(s_stressBars[i], v, LV_ANIM_OFF);
            }
        }
        for (uint8_t i = 0; i < EEZ_STRESS_TILES; ++i) {
            if (s_stressTileLbls[i]) {
                lv_label_set_text_fmt(s_stressTileLbls[i], "%02u", (unsigned)((s_phase * 3 + i * 23) % 100));
            }
        }
    }
}

} // namespace

REGISTER_APP_THEMED(
    eez_limits,
    "eez_limits",
    "EEZ Studio",
    LV_SYMBOL_LOOP,
    0x38BDF8,
    0x0891B2,
    buildAppEezLimits,
    tickAppEezLimits,
    refreshAppEezTheme,
    160
);
