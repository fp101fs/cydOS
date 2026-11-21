#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../drivers/ble_hid_mgr.h"
#include "../drivers/ble_mgr.h"
#include "../ui/status_bar.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// Trackpad + Classic Fire TV Media Remote (Standalone BLE HID App)
// ============================================================================

enum class PadMode : uint8_t {
    TRACKPAD = 0,
    REMOTE   = 1
};

static PadMode s_mode = PadMode::TRACKPAD;

static lv_obj_t* s_body          = nullptr;
static lv_obj_t* s_modeTrackBtn  = nullptr;
static lv_obj_t* s_modeRemoteBtn = nullptr;
static lv_obj_t* s_bleBadge      = nullptr;
static lv_obj_t* s_bleBadgeDot   = nullptr;
static lv_obj_t* s_bleBadgeLbl   = nullptr;

static lv_obj_t* s_trackView     = nullptr;
static lv_obj_t* s_remoteView    = nullptr;

// Trackpad surface widgets
static lv_obj_t* s_padArea       = nullptr;
static lv_obj_t* s_padStatusLbl  = nullptr;
static lv_obj_t* s_padReticle    = nullptr;
static lv_obj_t* s_scrollArea    = nullptr;
static lv_obj_t* s_scrollThumb   = nullptr;
static lv_obj_t* s_leftBtn       = nullptr;
static lv_obj_t* s_rightBtn      = nullptr;

// Trackpad drag / hold state (mirrors slide-to-select feel from m3_keyboard)
static bool     s_padDown        = false;
static bool     s_padDragActive  = false;
static int16_t  s_padStartX      = 0;
static int16_t  s_padStartY      = 0;
static int16_t  s_padLastX       = 0;
static int16_t  s_padLastY       = 0;
static uint32_t s_padDownMs      = 0;

// Scroll strip state
static bool     s_scrollDown     = false;
static int16_t  s_scrollLastY    = 0;
static uint32_t s_scrollLastMs   = 0;
static float    s_scrollAccum    = 0.0f;

// Connection UI tracking
static bool     s_prevActive     = false;
static bool     s_prevConnected  = false;

static void updateBleBadge();
static void updateModeUi();

// ============================================================================
// Trackpad Touch & Scroll Logic
// ============================================================================

static int8_t clampInt8(int32_t v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return (int8_t)v;
}

static void setPadVisualActive(bool active) {
    if (!s_padArea) return;
    if (active) {
        lv_obj_set_style_border_color(s_padArea, lv_color_hex(M3Color::CYAN), 0);
        lv_obj_set_style_border_width(s_padArea, 2, 0);
        if (s_padStatusLbl) {
            lv_label_set_text(s_padStatusLbl, bleHidIsConnected() ? "DRAG ACTIVE" : "PREVIEW (NOT PAIRED)");
            lv_obj_set_style_text_color(s_padStatusLbl, lv_color_hex(M3Color::CYAN), 0);
        }
    } else {
        lv_obj_set_style_border_color(s_padArea, lv_color_hex(m3OutlineHex()), 0);
        lv_obj_set_style_border_width(s_padArea, 1, 0);
        if (s_padStatusLbl) {
            lv_label_set_text(s_padStatusLbl, "Hold & drag  •  Tap = Click");
            lv_obj_set_style_text_color(s_padStatusLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        }
        if (s_padReticle) {
            lv_obj_add_flag(s_padReticle, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void updateReticlePos(int16_t absX, int16_t absY) {
    if (!s_padReticle || !s_padArea) return;
    lv_area_t area;
    lv_obj_get_coords(s_padArea, &area);
    int16_t rx = absX - area.x1 - 11;
    int16_t ry = absY - area.y1 - 11;
    int16_t maxX = (area.x2 - area.x1) - 22;
    int16_t maxY = (area.y2 - area.y1) - 22;
    if (rx < 0) rx = 0;
    if (rx > maxX) rx = maxX;
    if (ry < 0) ry = 0;
    if (ry > maxY) ry = maxY;
    lv_obj_set_pos(s_padReticle, rx, ry);
    lv_obj_clear_flag(s_padReticle, LV_OBJ_FLAG_HIDDEN);
}

static void padEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t pt = {0, 0};
    if (indev) lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        s_padDown       = true;
        s_padDragActive = false;
        s_padStartX     = pt.x;
        s_padStartY     = pt.y;
        s_padLastX      = pt.x;
        s_padLastY      = pt.y;
        s_padDownMs     = millis();
        updateReticlePos(pt.x, pt.y);
    } else if (code == LV_EVENT_PRESSING && s_padDown) {
        int16_t totalDx = pt.x - s_padStartX;
        int16_t totalDy = pt.y - s_padStartY;
        uint32_t heldMs = millis() - s_padDownMs;

        // Activate drag feedback once finger moves >3px or holds >=65ms
        if (!s_padDragActive && (abs(totalDx) >= 3 || abs(totalDy) >= 3 || heldMs >= 65)) {
            s_padDragActive = true;
            setPadVisualActive(true);
        }

        if (s_padDragActive) {
            updateReticlePos(pt.x, pt.y);
            int16_t dx = pt.x - s_padLastX;
            int16_t dy = pt.y - s_padLastY;
            if (dx != 0 || dy != 0) {
                // Mild non-linear acceleration for crisp precision + fast sweep
                int32_t mag = abs(dx) + abs(dy);
                float gain = (mag > 12) ? 2.4f : (mag > 5 ? 1.8f : 1.3f);
                int8_t sendX = clampInt8((int32_t)lroundf(dx * gain));
                int8_t sendY = clampInt8((int32_t)lroundf(dy * gain));
                if (sendX != 0 || sendY != 0) {
                    bleHidMouseMove(sendX, sendY, 0);
                }
                s_padLastX = pt.x;
                s_padLastY = pt.y;
            }
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_padDown) {
            int16_t totalDx = abs(pt.x - s_padStartX);
            int16_t totalDy = abs(pt.y - s_padStartY);
            uint32_t heldMs = millis() - s_padDownMs;
            // Quick tap without movement -> Left Click
            if (heldMs < 200 && totalDx < 6 && totalDy < 6) {
                m3PulseFeedback();
                bleHidMouseClick(BLE_MOUSE_LEFT);
            }
        }
        s_padDown = false;
        s_padDragActive = false;
        setPadVisualActive(false);
    }
}

static void scrollEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t pt = {0, 0};
    if (indev) lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        s_scrollDown   = true;
        s_scrollLastY  = pt.y;
        s_scrollLastMs = millis();
        s_scrollAccum  = 0.0f;
        lv_obj_set_style_border_color(s_scrollArea, lv_color_hex(M3Color::PRIMARY), 0);
        lv_obj_set_style_border_width(s_scrollArea, 2, 0);
    } else if (code == LV_EVENT_PRESSING && s_scrollDown) {
        int16_t dy = pt.y - s_scrollLastY;
        uint32_t now = millis();
        uint32_t dt = now - s_scrollLastMs;
        if (dt < 1) dt = 1;

        // Move visual scroll thumb inside strip
        if (s_scrollThumb && s_scrollArea) {
            lv_area_t area;
            lv_obj_get_coords(s_scrollArea, &area);
            int16_t ty = pt.y - area.y1 - 12;
            int16_t maxY = (area.y2 - area.y1) - 26;
            if (ty < 2) ty = 2;
            if (ty > maxY) ty = maxY;
            lv_obj_set_y(s_scrollThumb, ty);
        }

        if (dy != 0) {
            // Velocity (px/ms) boosts scroll proportionally to swipe speed & distance
            float speed = fabsf((float)dy) / (float)dt;
            float velBoost = 1.0f + fminf(speed * 2.5f, 3.0f);
            // Natural direction: swipe down -> scroll down (negative wheel in HID)
            s_scrollAccum += (-(float)dy / 5.5f) * velBoost;
            int32_t ticks = (int32_t)s_scrollAccum;
            if (ticks != 0) {
                s_scrollAccum -= (float)ticks;
                bleHidMouseMove(0, 0, clampInt8(ticks));
            }
            s_scrollLastY = pt.y;
            s_scrollLastMs = now;
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_scrollDown = false;
        s_scrollAccum = 0.0f;
        if (s_scrollArea) {
            lv_obj_set_style_border_color(s_scrollArea, lv_color_hex(m3OutlineHex()), 0);
            lv_obj_set_style_border_width(s_scrollArea, 1, 0);
        }
    }
}

static void mouseBtnEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (code == LV_EVENT_PRESSED) {
        m3PulseFeedback();
        bleHidMouseSetButtons(mask);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        bleHidMouseSetButtons(0);
    }
}

// ============================================================================
// Media Remote (Fire TV Stick Layout) Callbacks
// ============================================================================

static void remoteKeyCb(lv_event_t* e) {
    uint8_t hidKey = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (!bleHidIsConnected()) {
        m3ShowToast("Pair 'Pixel CYD Remote' in BT", 1300);
        return;
    }
    bleHidKeyTap(hidKey);
}

static void remoteConsumerCb(lv_event_t* e) {
    uint16_t usage = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (!bleHidIsConnected()) {
        m3ShowToast("Pair 'Pixel CYD Remote' in BT", 1300);
        return;
    }
    bleHidConsumerTap(usage);
}

static lv_obj_t* makeRemoteBtn(lv_obj_t* parent, const char* sym, lv_coord_t w, lv_coord_t h,
                               lv_coord_t radius, uint32_t bgHex, uint32_t fgHex,
                               lv_event_cb_t cb, uintptr_t arg) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, radius, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(M3Color::PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void*)arg);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fgHex), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
    return btn;
}

// ============================================================================
// Mode Toggle & BLE Badge
// ============================================================================

static void modeBtnCb(lv_event_t* e) {
    PadMode next = (PadMode)(uintptr_t)lv_event_get_user_data(e);
    if (next == s_mode) return;
    m3PulseFeedback();
    s_mode = next;
    updateModeUi();
}

static void bleBadgeCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (!g_sys.btEnabled || g_sys.airplaneMode) {
        bleMgrSetEnabled(true);
    }
    if (bleHidIsActive()) {
        bleHidStop();
        m3ShowToast("BLE HID stopped", 1000);
    } else {
        if (bleHidStart()) {
            m3ShowToast("Pair: Pixel CYD Remote", 1600);
        } else {
            m3ShowToast("BLE HID failed to start", 1300);
        }
    }
    statusBarRefresh();
    updateBleBadge();
}

static void updateBleBadge() {
    if (!s_bleBadgeDot || !s_bleBadgeLbl) return;
    bool conn = bleHidIsConnected();
    bool act  = bleHidIsActive();

    uint32_t dotCol = conn ? M3Color::TERTIARY : (act ? M3Color::AMBER : m3TextSecondaryHex());
    lv_obj_set_style_bg_color(s_bleBadgeDot, lv_color_hex(dotCol), 0);
    lv_label_set_text(s_bleBadgeLbl, bleHidStatusText());
}

static void updateModeUi() {
    if (!s_modeTrackBtn || !s_modeRemoteBtn || !s_trackView || !s_remoteView) return;
    bool isTrack = (s_mode == PadMode::TRACKPAD);

    lv_obj_set_style_bg_color(s_modeTrackBtn,
        lv_color_hex(isTrack ? M3Color::PRIMARY_CONT : m3SurfaceHighHex()), 0);
    lv_obj_t* tl = lv_obj_get_child(s_modeTrackBtn, 0);
    if (tl) lv_obj_set_style_text_color(tl, lv_color_hex(isTrack ? M3Color::PRIMARY : m3TextSecondaryHex()), 0);

    lv_obj_set_style_bg_color(s_modeRemoteBtn,
        lv_color_hex(!isTrack ? M3Color::SECONDARY_CONT : m3SurfaceHighHex()), 0);
    lv_obj_t* rl = lv_obj_get_child(s_modeRemoteBtn, 0);
    if (rl) lv_obj_set_style_text_color(rl, lv_color_hex(!isTrack ? M3Color::SECONDARY : m3TextSecondaryHex()), 0);

    if (isTrack) {
        lv_obj_clear_flag(s_trackView, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_remoteView, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_trackView, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_remoteView, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================================================
// Build Trackpad View & Fire TV Media Remote View
// ============================================================================

static void buildTrackpadView(lv_obj_t* parent) {
    s_trackView = lv_obj_create(parent);
    lv_obj_set_size(s_trackView, LV_PCT(100), 160);
    lv_obj_set_style_bg_opa(s_trackView, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_trackView, 0, 0);
    lv_obj_set_style_pad_all(s_trackView, 0, 0);
    lv_obj_clear_flag(s_trackView, LV_OBJ_FLAG_SCROLLABLE);

    // Top row: 3/4 Trackpad surface + 1/4 Right Scroll Strip (total width 220px)
    s_padArea = lv_obj_create(s_trackView);
    lv_obj_set_size(s_padArea, 162, 118);
    lv_obj_set_pos(s_padArea, 0, 0);
    lv_obj_set_style_bg_color(s_padArea, lv_color_hex(m3SurfaceHex()), 0);
    lv_obj_set_style_bg_opa(s_padArea, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_padArea, 14, 0);
    lv_obj_set_style_border_width(s_padArea, 1, 0);
    lv_obj_set_style_border_color(s_padArea, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_pad_all(s_padArea, 0, 0);
    lv_obj_clear_flag(s_padArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_padArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_padArea, padEventCb, LV_EVENT_ALL, nullptr);

    s_padStatusLbl = lv_label_create(s_padArea);
    lv_label_set_text(s_padStatusLbl, "Hold & drag  •  Tap = Click");
    lv_obj_set_style_text_color(s_padStatusLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(s_padStatusLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_padStatusLbl, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(s_padStatusLbl, LV_OBJ_FLAG_CLICKABLE);

    // Live drag reticle inside pad
    s_padReticle = lv_obj_create(s_padArea);
    lv_obj_set_size(s_padReticle, 22, 22);
    lv_obj_set_style_radius(s_padReticle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_padReticle, lv_color_hex(M3Color::CYAN), 0);
    lv_obj_set_style_bg_opa(s_padReticle, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_padReticle, 2, 0);
    lv_obj_set_style_border_color(s_padReticle, lv_color_hex(M3Color::CYAN), 0);
    lv_obj_set_style_pad_all(s_padReticle, 0, 0);
    lv_obj_clear_flag(s_padReticle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_padReticle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_padReticle, LV_OBJ_FLAG_HIDDEN);

    // Right 1/4 Vertical Scroll Strip (52x118)
    s_scrollArea = lv_obj_create(s_trackView);
    lv_obj_set_size(s_scrollArea, 52, 118);
    lv_obj_set_pos(s_scrollArea, 168, 0);
    lv_obj_set_style_bg_color(s_scrollArea, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(s_scrollArea, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_scrollArea, 14, 0);
    lv_obj_set_style_border_width(s_scrollArea, 1, 0);
    lv_obj_set_style_border_color(s_scrollArea, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_pad_all(s_scrollArea, 0, 0);
    lv_obj_clear_flag(s_scrollArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scrollArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scrollArea, scrollEventCb, LV_EVENT_ALL, nullptr);

    lv_obj_t* upSym = lv_label_create(s_scrollArea);
    lv_label_set_text(upSym, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(upSym, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(upSym, &lv_font_montserrat_10, 0);
    lv_obj_align(upSym, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_clear_flag(upSym, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* scLbl = lv_label_create(s_scrollArea);
    lv_label_set_text(scLbl, "SCR");
    lv_obj_set_style_text_color(scLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(scLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(scLbl);
    lv_obj_clear_flag(scLbl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* dnSym = lv_label_create(s_scrollArea);
    lv_label_set_text(dnSym, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(dnSym, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(dnSym, &lv_font_montserrat_10, 0);
    lv_obj_align(dnSym, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_clear_flag(dnSym, LV_OBJ_FLAG_CLICKABLE);

    // Visual scroll thumb pill
    s_scrollThumb = lv_obj_create(s_scrollArea);
    lv_obj_set_size(s_scrollThumb, 6, 24);
    lv_obj_set_pos(s_scrollThumb, 40, 47);
    lv_obj_set_style_radius(s_scrollThumb, 3, 0);
    lv_obj_set_style_bg_color(s_scrollThumb, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_bg_opa(s_scrollThumb, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_scrollThumb, 0, 0);
    lv_obj_clear_flag(s_scrollThumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_scrollThumb, LV_OBJ_FLAG_SCROLLABLE);

    // Bottom Row: 2 Big Half-Width Mouse Click Buttons (Left + Right)
    s_leftBtn = lv_btn_create(s_trackView);
    lv_obj_set_size(s_leftBtn, 107, 36);
    lv_obj_set_pos(s_leftBtn, 0, 124);
    lv_obj_set_style_radius(s_leftBtn, 12, 0);
    lv_obj_set_style_bg_color(s_leftBtn, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_color(s_leftBtn, lv_color_hex(M3Color::PRIMARY_CONT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_leftBtn, 1, 0);
    lv_obj_set_style_border_color(s_leftBtn, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_shadow_width(s_leftBtn, 0, 0);
    lv_obj_add_event_cb(s_leftBtn, mouseBtnEventCb, LV_EVENT_ALL, (void*)(uintptr_t)BLE_MOUSE_LEFT);

    lv_obj_t* lLbl = lv_label_create(s_leftBtn);
    lv_label_set_text(lLbl, "Left Click");
    lv_obj_set_style_text_color(lLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(lLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lLbl);

    s_rightBtn = lv_btn_create(s_trackView);
    lv_obj_set_size(s_rightBtn, 107, 36);
    lv_obj_set_pos(s_rightBtn, 113, 124);
    lv_obj_set_style_radius(s_rightBtn, 12, 0);
    lv_obj_set_style_bg_color(s_rightBtn, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_color(s_rightBtn, lv_color_hex(M3Color::PRIMARY_CONT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_rightBtn, 1, 0);
    lv_obj_set_style_border_color(s_rightBtn, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_shadow_width(s_rightBtn, 0, 0);
    lv_obj_add_event_cb(s_rightBtn, mouseBtnEventCb, LV_EVENT_ALL, (void*)(uintptr_t)BLE_MOUSE_RIGHT);

    lv_obj_t* rLbl = lv_label_create(s_rightBtn);
    lv_label_set_text(rLbl, "Right Click");
    lv_obj_set_style_text_color(rLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(rLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(rLbl);
}

static void buildRemoteView(lv_obj_t* parent) {
    s_remoteView = lv_obj_create(parent);
    lv_obj_set_size(s_remoteView, LV_PCT(100), 160);
    lv_obj_set_style_bg_opa(s_remoteView, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_remoteView, 0, 0);
    lv_obj_set_style_pad_all(s_remoteView, 0, 0);
    lv_obj_clear_flag(s_remoteView, LV_OBJ_FLAG_SCROLLABLE);

    // Classic Amazon Fire TV Stick Remote layout (compact 220x160):
    // Left column: Power + Volume rocker; Center: Circular D-Pad; Bottom: Nav + Media rows

    // Power (top-left) & Mute (top-right)
    lv_obj_t* pwrBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_POWER, 38, 28, 14,
                                     0x3B1D22, M3Color::CORAL, remoteConsumerCb, HID_CC_POWER);
    lv_obj_set_pos(pwrBtn, 2, 2);

    lv_obj_t* muteBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_MUTE, 38, 28, 14,
                                      m3SurfaceHighHex(), m3TextPrimaryHex(), remoteConsumerCb, HID_CC_MUTE);
    lv_obj_set_pos(muteBtn, 180, 2);

    // Volume Up & Down on left/right edges flanking D-pad
    lv_obj_t* volUpBtn = makeRemoteBtn(s_remoteView, "V+", 38, 30, 10,
                                       m3SurfaceHighHex(), M3Color::TERTIARY, remoteConsumerCb, HID_CC_VOL_UP);
    lv_obj_set_pos(volUpBtn, 2, 36);

    lv_obj_t* volDnBtn = makeRemoteBtn(s_remoteView, "V-", 38, 30, 10,
                                       m3SurfaceHighHex(), m3TextSecondaryHex(), remoteConsumerCb, HID_CC_VOL_DOWN);
    lv_obj_set_pos(volDnBtn, 2, 70);

    // Circular D-Pad Ring container (centered: x=58, y=0, 104x104)
    lv_obj_t* ring = lv_obj_create(s_remoteView);
    lv_obj_set_size(ring, 104, 104);
    lv_obj_set_pos(ring, 58, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(m3SurfaceHex()), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dUp = makeRemoteBtn(ring, LV_SYMBOL_UP, 34, 24, 10,
                                  m3SurfaceHighHex(), m3TextPrimaryHex(), remoteKeyCb, HID_KEY_UP);
    lv_obj_align(dUp, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* dDn = makeRemoteBtn(ring, LV_SYMBOL_DOWN, 34, 24, 10,
                                  m3SurfaceHighHex(), m3TextPrimaryHex(), remoteKeyCb, HID_KEY_DOWN);
    lv_obj_align(dDn, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_t* dLt = makeRemoteBtn(ring, LV_SYMBOL_LEFT, 24, 34, 10,
                                  m3SurfaceHighHex(), m3TextPrimaryHex(), remoteKeyCb, HID_KEY_LEFT);
    lv_obj_align(dLt, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* dRt = makeRemoteBtn(ring, LV_SYMBOL_RIGHT, 24, 34, 10,
                                  m3SurfaceHighHex(), m3TextPrimaryHex(), remoteKeyCb, HID_KEY_RIGHT);
    lv_obj_align(dRt, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_t* dOk = makeRemoteBtn(ring, "OK", 38, 38, LV_RADIUS_CIRCLE,
                                  M3Color::PRIMARY_CONT, M3Color::PRIMARY, remoteKeyCb, HID_KEY_ENTER);
    lv_obj_center(dOk);

    // Quick Back/Home/Menu row (Fire TV middle triad)
    lv_obj_t* backBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_LEFT " Back", 68, 24, 12,
                                      m3SurfaceHighHex(), m3TextPrimaryHex(), remoteConsumerCb, HID_CC_AC_BACK);
    lv_obj_set_pos(backBtn, 2, 108);

    lv_obj_t* homeBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_HOME " Home", 72, 24, 12,
                                      M3Color::SECONDARY_CONT, M3Color::SECONDARY, remoteConsumerCb, HID_CC_AC_HOME);
    lv_obj_set_pos(homeBtn, 74, 108);

    lv_obj_t* menuBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_LIST " Menu", 68, 24, 12,
                                      m3SurfaceHighHex(), m3TextPrimaryHex(), remoteConsumerCb, HID_CC_MENU);
    lv_obj_set_pos(menuBtn, 150, 108);

    // Media Transport Row (Rewind / Play-Pause / Fast-Forward)
    lv_obj_t* rewBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_PREV, 68, 24, 12,
                                     m3SurfaceHighHex(), m3TextPrimaryHex(), remoteConsumerCb, HID_CC_REWIND);
    lv_obj_set_pos(rewBtn, 2, 136);

    lv_obj_t* playBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, 72, 24, 12,
                                      M3Color::PRIMARY_CONT, M3Color::PRIMARY, remoteConsumerCb, HID_CC_PLAY_PAUSE);
    lv_obj_set_pos(playBtn, 74, 136);

    lv_obj_t* ffBtn = makeRemoteBtn(s_remoteView, LV_SYMBOL_NEXT, 68, 24, 12,
                                    m3SurfaceHighHex(), m3TextPrimaryHex(), remoteConsumerCb, HID_CC_FAST_FWD);
    lv_obj_set_pos(ffBtn, 150, 136);
}

// ============================================================================
// App Lifecycle
// ============================================================================

static void refreshAppTrackpadTheme() {
    if (!s_body) return;
    if (s_padArea) {
        lv_obj_set_style_bg_color(s_padArea, lv_color_hex(m3SurfaceHex()), 0);
        setPadVisualActive(s_padDragActive);
    }
    if (s_scrollArea) {
        lv_obj_set_style_bg_color(s_scrollArea, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_scrollArea, lv_color_hex(m3OutlineHex()), 0);
    }
    updateModeUi();
    updateBleBadge();
}

static void trackpadDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    s_modeTrackBtn = nullptr;
    s_modeRemoteBtn = nullptr;
    s_bleBadge = nullptr;
    s_bleBadgeDot = nullptr;
    s_bleBadgeLbl = nullptr;
    s_trackView = nullptr;
    s_remoteView = nullptr;
    s_padArea = nullptr;
    s_padStatusLbl = nullptr;
    s_padReticle = nullptr;
    s_scrollArea = nullptr;
    s_scrollThumb = nullptr;
    s_leftBtn = nullptr;
    s_rightBtn = nullptr;
    s_padDown = false;
    s_padDragActive = false;
    s_scrollDown = false;
}

static void buildAppTrackpad(lv_obj_t* body) {
    s_body = body;
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, trackpadDeleteCb, LV_EVENT_DELETE, nullptr);

    // Auto-start BLE HID advertising if Bluetooth is enabled
    if (g_sys.btEnabled && !g_sys.airplaneMode && !bleHidIsActive()) {
        bleHidStart();
    }

    // Top bar: Mode selector (Trackpad / Remote) + BLE Status badge
    lv_obj_t* topRow = lv_obj_create(body);
    lv_obj_set_size(topRow, LV_PCT(100), 26);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* seg = lv_obj_create(topRow);
    lv_obj_set_size(seg, 134, 24);
    lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(seg, 0, 0);
    lv_obj_set_style_pad_all(seg, 0, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(seg, 4, 0);
    lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);

    s_modeTrackBtn = m3PillBtn(seg, "Pad", M3Color::PRIMARY_CONT, M3Color::PRIMARY, 60, 24);
    lv_obj_add_event_cb(s_modeTrackBtn, modeBtnCb, LV_EVENT_CLICKED, (void*)(uintptr_t)PadMode::TRACKPAD);

    s_modeRemoteBtn = m3PillBtn(seg, "Remote", m3SurfaceHighHex(), m3TextSecondaryHex(), 68, 24);
    lv_obj_add_event_cb(s_modeRemoteBtn, modeBtnCb, LV_EVENT_CLICKED, (void*)(uintptr_t)PadMode::REMOTE);

    // Clickable BLE HID status pill
    s_bleBadge = lv_btn_create(topRow);
    lv_obj_set_size(s_bleBadge, 82, 24);
    lv_obj_set_style_radius(s_bleBadge, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(s_bleBadge, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_border_width(s_bleBadge, 1, 0);
    lv_obj_set_style_border_color(s_bleBadge, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_shadow_width(s_bleBadge, 0, 0);
    lv_obj_set_style_pad_hor(s_bleBadge, 6, 0);
    lv_obj_set_flex_flow(s_bleBadge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bleBadge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_bleBadge, 4, 0);
    lv_obj_add_event_cb(s_bleBadge, bleBadgeCb, LV_EVENT_CLICKED, nullptr);

    s_bleBadgeDot = lv_obj_create(s_bleBadge);
    lv_obj_set_size(s_bleBadgeDot, 7, 7);
    lv_obj_set_style_radius(s_bleBadgeDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_bleBadgeDot, 0, 0);
    lv_obj_clear_flag(s_bleBadgeDot, LV_OBJ_FLAG_CLICKABLE);

    s_bleBadgeLbl = lv_label_create(s_bleBadge);
    lv_label_set_text(s_bleBadgeLbl, "Pair");
    lv_obj_set_style_text_color(s_bleBadgeLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(s_bleBadgeLbl, &lv_font_montserrat_10, 0);

    // Content stack
    lv_obj_t* stack = lv_obj_create(body);
    lv_obj_set_size(stack, LV_PCT(100), 160);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_clear_flag(stack, LV_OBJ_FLAG_SCROLLABLE);

    buildTrackpadView(stack);
    buildRemoteView(stack);

    s_prevActive = bleHidIsActive();
    s_prevConnected = bleHidIsConnected();
    updateModeUi();
    updateBleBadge();
}

static void tickAppTrackpad() {
    if (!s_body) return;
    bool act  = bleHidIsActive();
    bool conn = bleHidIsConnected();
    if (act != s_prevActive || conn != s_prevConnected) {
        s_prevActive = act;
        s_prevConnected = conn;
        updateBleBadge();
        if (conn) {
            m3ShowToast("BLE Host Connected", 1200);
        }
    }
}

REGISTER_APP_THEMED(trackpad, "trackpad", "Trackpad", LV_SYMBOL_GPS, 0x78D9EC, 0x0E7490, buildAppTrackpad, tickAppTrackpad, refreshAppTrackpadTheme, 55);
