#ifndef M3_MODAL_HOST_TEST
#include "m3_modal.h"
#include "m3_widgets.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#endif
#include <stdio.h>
#include <string.h>

static constexpr uint8_t  MODAL_MAX_BTNS         = 4;
static constexpr uint8_t  MODAL_MAX_STEPS        = 12;
static constexpr uint32_t PROG_BAR_ANIM_MS       = 120;
static constexpr uint32_t PROG_INDET_ANIM_MS     = 1100;
static constexpr uint32_t PROG_PUMP_INTERVAL_MS  = 25;

static lv_obj_t* s_backdrop      = nullptr;
static lv_obj_t* s_card          = nullptr;
static lv_obj_t* s_titleLbl      = nullptr;
static lv_obj_t* s_closeBtn      = nullptr;
static lv_obj_t* s_bodyWrap      = nullptr;
static lv_obj_t* s_bodyLbl       = nullptr;
static lv_obj_t* s_footer        = nullptr;
static lv_obj_t* s_pageLbl       = nullptr;
static lv_obj_t* s_prevBtn       = nullptr;
static lv_obj_t* s_nextBtn       = nullptr;
static lv_obj_t* s_nextLbl       = nullptr;

// Progress modal widgets & state
static lv_obj_t* s_progBar       = nullptr;
static lv_obj_t* s_progMetaRow   = nullptr;
static lv_obj_t* s_progDetailLbl = nullptr;
static lv_obj_t* s_progPctLbl    = nullptr;
static lv_obj_t* s_cancelBtn     = nullptr;

static M3ModalBtn  s_btns[MODAL_MAX_BTNS];
static M3ModalStep s_steps[MODAL_MAX_STEPS];
static uint8_t     s_stepCount         = 0;
static uint8_t     s_stepIdx           = 0;
static uint32_t    s_accentHex         = 0;
static bool        s_progressOpen      = false;
static bool        s_progressLocked    = false;
static bool        s_progIndeterminate = false;
static int8_t      s_progLastPct       = -1;
static uint32_t    s_lastPumpMs        = 0;
static void      (*s_onCancel)()       = nullptr;

static void indeterminateAnimCb(void* obj, int32_t v) {
    lv_obj_t* bar = (lv_obj_t*)obj;
    if (!bar) return;
    int32_t start = v;
    if (start < 0) start = 0;
    if (start > 100) start = 100;
    int32_t end = v + 30;
    if (end < 0) end = 0;
    if (end > 100) end = 100;
    lv_bar_set_start_value(bar, start, LV_ANIM_OFF);
    lv_bar_set_value(bar, end, LV_ANIM_OFF);
}

static void startIndeterminateAnim() {
    if (!s_progBar) return;
    lv_anim_del(s_progBar, indeterminateAnimCb);
    lv_bar_set_mode(s_progBar, LV_BAR_MODE_RANGE);
    s_progIndeterminate = true;
    s_progLastPct = -1;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_progBar);
    lv_anim_set_values(&a, -30, 100);
    lv_anim_set_time(&a, PROG_INDET_ANIM_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, indeterminateAnimCb);
    lv_anim_start(&a);
}

static void stopIndeterminateAnim() {
    if (!s_progBar) return;
    lv_anim_del(s_progBar, indeterminateAnimCb);
    s_progIndeterminate = false;
    lv_bar_set_mode(s_progBar, LV_BAR_MODE_NORMAL);
    lv_bar_set_start_value(s_progBar, 0, LV_ANIM_OFF);
}

static void triggerCancel() {
    if (s_onCancel) {
        void (*cb)() = s_onCancel;
        s_onCancel = nullptr;
        if (s_cancelBtn) {
            lv_obj_add_state(s_cancelBtn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(s_cancelBtn, LV_OPA_50, 0);
            lv_obj_t* lbl = lv_obj_get_child(s_cancelBtn, 0);
            if (lbl) lv_label_set_text(lbl, "Cancelling...");
        }
        if (s_closeBtn) {
            lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN);
        }
        cb();
        return;
    }
    m3ModalClose();
}

static void closeBtnCb(lv_event_t* e) {
    (void)e;
    if (s_progressLocked) return;
    m3PulseFeedback();
    if (s_progressOpen && s_onCancel) {
        triggerCancel();
        return;
    }
    m3ModalClose();
}

static void cancelBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    triggerCancel();
}

static void actionBtnCb(lv_event_t* e) {
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (idx >= MODAL_MAX_BTNS) {
        m3ModalClose();
        return;
    }
    void (*cb)() = s_btns[idx].onClick;
    bool shouldClose = s_btns[idx].closeOnClick;
    if (shouldClose) m3ModalClose();
    if (cb) cb();
}

static void updateStepUi() {
    if (!s_card || s_stepCount == 0 || s_stepIdx >= s_stepCount) return;

    const M3ModalStep& st = s_steps[s_stepIdx];
    if (s_titleLbl) lv_label_set_text(s_titleLbl, st.title ? st.title : "");
    if (s_bodyLbl)  lv_label_set_text(s_bodyLbl, st.body ? st.body : "");

    if (s_pageLbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(s_stepIdx + 1), (unsigned)s_stepCount);
        lv_label_set_text(s_pageLbl, buf);
    }

    if (s_prevBtn) {
        if (s_stepIdx == 0) {
            lv_obj_add_state(s_prevBtn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(s_prevBtn, LV_OPA_30, 0);
        } else {
            lv_obj_clear_state(s_prevBtn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(s_prevBtn, LV_OPA_COVER, 0);
        }
    }

    if (s_nextLbl) {
        lv_label_set_text(s_nextLbl, (s_stepIdx + 1 >= s_stepCount) ? "Done" : "Next");
    }
}

static void prevStepCb(lv_event_t* e) {
    (void)e;
    if (s_stepIdx == 0) return;
    m3PulseFeedback();
    --s_stepIdx;
    updateStepUi();
}

static void nextStepCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (s_stepIdx + 1 >= s_stepCount) {
        m3ModalClose();
        return;
    }
    ++s_stepIdx;
    updateStepUi();
}

static lv_obj_t* createModalShell(const char* title, uint32_t accentHex) {
    m3ModalClose();

    s_accentHex = accentHex ? accentHex : m3PrimaryHex();

    uint16_t scrW = displayWidth();
    uint16_t scrH = displayHeight();
    int16_t modalW = (int16_t)((scrW * 3) / 4);   // 3/4 screen width (180px on 240)
    int16_t modalH = (int16_t)((scrH * 2) / 3);   // 2/3 screen height (213px on 320)

    s_backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_backdrop, scrW, scrH);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_backdrop, 0, 0);
    lv_obj_set_style_radius(s_backdrop, 0, 0);
    lv_obj_set_style_pad_all(s_backdrop, 0, 0);
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_backdrop);

    s_card = lv_obj_create(s_backdrop);
    lv_obj_set_size(s_card, modalW, modalH);
    lv_obj_center(s_card);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(m3SurfaceHex()), 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card, 18, 0);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_border_opa(s_card, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(s_card, 20, 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_y(s_card, 4, 0);
    lv_obj_set_style_shadow_color(s_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(s_card, 12, 0);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_card, 8, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    // Header row: Title + top-right X close button
    lv_obj_t* hdr = lv_obj_create(s_card);
    lv_obj_set_size(hdr, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    s_titleLbl = lv_label_create(hdr);
    lv_label_set_text(s_titleLbl, title ? title : "");
    lv_label_set_long_mode(s_titleLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_titleLbl, 1);
    lv_obj_set_style_text_font(s_titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_titleLbl, lv_color_hex(s_accentHex), 0);

    s_closeBtn = lv_btn_create(hdr);
    lv_obj_set_size(s_closeBtn, 26, 26);
    lv_obj_set_style_radius(s_closeBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(s_closeBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
    lv_obj_set_style_bg_opa(s_closeBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_closeBtn, 0, 0);
    lv_obj_set_style_pad_all(s_closeBtn, 0, 0);
    lv_obj_add_event_cb(s_closeBtn, closeBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* xLbl = lv_label_create(s_closeBtn);
    lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(xLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(xLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_center(xLbl);

    // Subtle divider
    lv_obj_t* div = lv_obj_create(s_card);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_40, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Body container (vertically centered)
    s_bodyWrap = lv_obj_create(s_card);
    lv_obj_set_width(s_bodyWrap, LV_PCT(100));
    lv_obj_set_flex_grow(s_bodyWrap, 1);
    lv_obj_set_style_bg_opa(s_bodyWrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bodyWrap, 0, 0);
    lv_obj_set_style_pad_hor(s_bodyWrap, 2, 0);
    lv_obj_set_style_pad_ver(s_bodyWrap, 0, 0);
    lv_obj_set_flex_flow(s_bodyWrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_bodyWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_bodyWrap, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_bodyWrap, LV_SCROLLBAR_MODE_AUTO);

    s_bodyLbl = lv_label_create(s_bodyWrap);
    lv_obj_set_width(s_bodyLbl, LV_PCT(100));
    lv_label_set_long_mode(s_bodyLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_bodyLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_bodyLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_line_space(s_bodyLbl, 4, 0);

    // Footer container
    s_footer = lv_obj_create(s_card);
    lv_obj_set_size(s_footer, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(s_footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_footer, 0, 0);
    lv_obj_set_style_pad_all(s_footer, 0, 0);
    lv_obj_set_flex_flow(s_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_footer, 6, 0);
    lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_SCROLLABLE);

    return s_footer;
}

static void populateActionButtons(lv_obj_t* footer,
                                  const M3ModalBtn* buttons,
                                  uint8_t buttonCount,
                                  const char* defaultLabel,
                                  uint32_t defaultBgHex,
                                  uint32_t defaultFgHex) {
    if (!footer) return;
    if (!buttons || buttonCount == 0) {
        lv_obj_t* okBtn = m3PillBtn(footer, defaultLabel, defaultBgHex, defaultFgHex, LV_PCT(100), 30);
        lv_obj_add_event_cb(okBtn, closeBtnCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    if (buttonCount > MODAL_MAX_BTNS) buttonCount = MODAL_MAX_BTNS;
    for (uint8_t i = 0; i < buttonCount; ++i) {
        s_btns[i] = buttons[i];
        uint32_t bg = buttons[i].bgHex ? buttons[i].bgHex : m3SurfaceHighestHex();
        uint32_t fg = buttons[i].fgHex ? buttons[i].fgHex : m3TextPrimaryHex();
        lv_obj_t* btn = m3PillBtn(footer, buttons[i].label ? buttons[i].label : "OK", bg, fg, 0, 30);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_add_event_cb(btn, actionBtnCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void m3ModalShow(const char* title,
                 const char* body,
                 const M3ModalBtn* buttons,
                 uint8_t buttonCount,
                 uint32_t accentHex) {
    lv_obj_t* footer = createModalShell(title, accentHex);
    lv_label_set_text(s_bodyLbl, body ? body : "");

    s_stepCount = 0;
    s_stepIdx = 0;

    populateActionButtons(footer, buttons, buttonCount, "OK", s_accentHex, 0xFFFFFF);
}

void m3ModalShowSteps(const M3ModalStep* steps,
                      uint8_t stepCount,
                      uint32_t accentHex) {
    if (!steps || stepCount == 0) return;
    if (stepCount > MODAL_MAX_STEPS) stepCount = MODAL_MAX_STEPS;

    lv_obj_t* footer = createModalShell(steps[0].title, accentHex);

    for (uint8_t i = 0; i < stepCount; ++i) {
        s_steps[i] = steps[i];
    }
    s_stepCount = stepCount;
    s_stepIdx = 0;

    s_prevBtn = m3PillBtn(footer, "Prev", m3SurfaceHighestHex(), m3TextPrimaryHex(), 52, 28);
    lv_obj_add_event_cb(s_prevBtn, prevStepCb, LV_EVENT_CLICKED, nullptr);

    s_pageLbl = lv_label_create(footer);
    lv_obj_set_style_text_font(s_pageLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_pageLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    s_nextBtn = m3PillBtn(footer, "Next", s_accentHex, 0xFFFFFF, 52, 28);
    s_nextLbl = lv_obj_get_child(s_nextBtn, 0);
    lv_obj_add_event_cb(s_nextBtn, nextStepCb, LV_EVENT_CLICKED, nullptr);

    updateStepUi();
}

void m3ModalShowProgress(const char* title,
                         const char* stepText,
                         uint32_t accentHex,
                         bool cancellable,
                         void (*onCancel)()) {
    lv_obj_t* footer = createModalShell(title ? title : "Working...", accentHex);

    s_progressOpen   = true;
    s_progressLocked = !cancellable;
    s_onCancel       = cancellable ? onCancel : nullptr;

    if (!cancellable && s_closeBtn) {
        lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_pad_row(s_bodyWrap, 8, 0);
    lv_label_set_text(s_bodyLbl, (stepText && stepText[0]) ? stepText : "Starting...");

    // Animated Material 3 linear progress bar
    s_progBar = lv_bar_create(s_bodyWrap);
    lv_obj_set_size(s_progBar, LV_PCT(100), 10);
    lv_bar_set_range(s_progBar, 0, 100);
    lv_obj_set_style_radius(s_progBar, M3Dim::RADIUS_PILL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progBar, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progBar, M3Dim::RADIUS_PILL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progBar, lv_color_hex(s_accentHex), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_progBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(s_progBar, PROG_BAR_ANIM_MS, 0);

    // Secondary detail (left) + percentage (right)
    s_progMetaRow = lv_obj_create(s_bodyWrap);
    lv_obj_set_size(s_progMetaRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_progMetaRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_progMetaRow, 0, 0);
    lv_obj_set_style_pad_all(s_progMetaRow, 0, 0);
    lv_obj_set_flex_flow(s_progMetaRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_progMetaRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_progMetaRow, 6, 0);
    lv_obj_clear_flag(s_progMetaRow, LV_OBJ_FLAG_SCROLLABLE);

    s_progDetailLbl = lv_label_create(s_progMetaRow);
    lv_label_set_text(s_progDetailLbl, "");
    lv_label_set_long_mode(s_progDetailLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_progDetailLbl, 1);
    lv_obj_set_style_text_font(s_progDetailLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_progDetailLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    s_progPctLbl = lv_label_create(s_progMetaRow);
    lv_label_set_text(s_progPctLbl, "...");
    lv_obj_set_style_text_font(s_progPctLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_progPctLbl, lv_color_hex(s_accentHex), 0);

    startIndeterminateAnim();

    if (cancellable) {
        s_cancelBtn = m3PillBtn(footer, "Cancel", m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 30);
        lv_obj_add_event_cb(s_cancelBtn, cancelBtnCb, LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* waitLbl = lv_label_create(footer);
        lv_label_set_text(waitLbl, "Please wait...");
        lv_obj_set_style_text_font(waitLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(waitLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    }

    lv_timer_handler();
    s_lastPumpMs = lv_tick_get();
}

void m3ModalUpdateProgress(int8_t pct,
                           const char* stepText,
                           const char* detailText,
                           bool pumpUi) {
    if (!s_progressOpen || !s_progBar) return;

    bool stepChanged = false;
    if (stepText && s_bodyLbl) {
        const char* cur = lv_label_get_text(s_bodyLbl);
        if (!cur || strcmp(cur, stepText) != 0) {
            lv_label_set_text(s_bodyLbl, stepText);
            stepChanged = true;
        }
    }

    if (detailText && s_progDetailLbl) {
        const char* cur = lv_label_get_text(s_progDetailLbl);
        if (!cur || strcmp(cur, detailText) != 0) {
            lv_label_set_text(s_progDetailLbl, detailText);
        }
    }

    bool reachedEnd = false;
    if (pct < 0) {
        if (!s_progIndeterminate) {
            startIndeterminateAnim();
            if (s_progPctLbl) lv_label_set_text(s_progPctLbl, "...");
        }
    } else {
        int8_t clamped = (pct > 100) ? 100 : pct;
        if (s_progIndeterminate) {
            stopIndeterminateAnim();
        }
        if (clamped != s_progLastPct) {
            lv_bar_set_value(s_progBar, clamped, LV_ANIM_ON);
            if (s_progPctLbl) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", (int)clamped);
                lv_label_set_text(s_progPctLbl, buf);
            }
            s_progLastPct = clamped;
            reachedEnd = (clamped == 100);
        }
    }

    if (pumpUi) {
        uint32_t now = lv_tick_get();
        if (stepChanged || reachedEnd || (uint32_t)(now - s_lastPumpMs) >= PROG_PUMP_INTERVAL_MS) {
            lv_timer_handler();
            s_lastPumpMs = lv_tick_get();
        }
    }
}

void m3ModalSetProgressLocked(bool locked) {
    if (!s_progressOpen) return;
    s_progressLocked = locked;
    if (locked) {
        s_onCancel = nullptr;
        if (s_closeBtn) {
            lv_obj_add_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_cancelBtn && s_footer) {
            s_cancelBtn = nullptr;
            lv_obj_clean(s_footer);
            lv_obj_set_flex_align(s_footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* waitLbl = lv_label_create(s_footer);
            lv_label_set_text(waitLbl, "Please wait...");
            lv_obj_set_style_text_font(waitLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(waitLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        }
        lv_timer_handler();
        s_lastPumpMs = lv_tick_get();
    } else if (s_closeBtn) {
        lv_obj_clear_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

void m3ModalFinishProgress(bool success,
                           const char* headline,
                           const char* detail,
                           const M3ModalBtn* buttons,
                           uint8_t buttonCount) {
    uint32_t stateHex = success ? M3Color::TERTIARY : M3Color::CORAL;

    if (!s_progressOpen || !s_card || !s_progBar) {
        const char* fallbackTitle = headline ? headline : (success ? "Done" : "Error");
        const char* fallbackBody  = detail ? detail : (success ? "Completed." : "Operation failed.");
        m3ModalShow(fallbackTitle, fallbackBody, buttons, buttonCount, stateHex);
        return;
    }

    bool wasIndeterminate = s_progIndeterminate;
    stopIndeterminateAnim();
    s_progressLocked = false;
    s_onCancel       = nullptr;
    s_cancelBtn      = nullptr;

    if (s_closeBtn) {
        lv_obj_clear_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_titleLbl) {
        if (headline) lv_label_set_text(s_titleLbl, headline);
        lv_obj_set_style_text_color(s_titleLbl, lv_color_hex(stateHex), 0);
    }

    if (s_bodyLbl) {
        if (detail) {
            lv_label_set_text(s_bodyLbl, detail);
        } else if (!success && s_progLastPct < 0) {
            lv_label_set_text(s_bodyLbl, "Operation failed.");
        }
    }

    lv_obj_set_style_bg_color(s_progBar, lv_color_hex(stateHex), LV_PART_INDICATOR);
    if (success) {
        lv_bar_set_value(s_progBar, 100, LV_ANIM_ON);
        s_progLastPct = 100;
    } else if (wasIndeterminate) {
        lv_bar_set_value(s_progBar, 0, LV_ANIM_OFF);
        s_progLastPct = 0;
    }

    if (s_progPctLbl) {
        lv_label_set_text(s_progPctLbl, success ? LV_SYMBOL_OK " 100%" : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(s_progPctLbl, lv_color_hex(stateHex), 0);
    }

    if (s_footer) {
        lv_obj_clean(s_footer);
        lv_obj_set_flex_align(s_footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        populateActionButtons(s_footer, buttons, buttonCount,
                              success ? "Done" : "Close",
                              stateHex, 0x0F1117);
    }

    lv_timer_handler();
    s_lastPumpMs = lv_tick_get();
}

void m3ModalClose() {
    if (s_progBar) {
        lv_anim_del(s_progBar, nullptr);
    }
    if (s_backdrop) {
        lv_obj_del(s_backdrop);
        s_backdrop = nullptr;
    }
    s_card              = nullptr;
    s_titleLbl          = nullptr;
    s_closeBtn          = nullptr;
    s_bodyWrap          = nullptr;
    s_bodyLbl           = nullptr;
    s_footer            = nullptr;
    s_pageLbl           = nullptr;
    s_prevBtn           = nullptr;
    s_nextBtn           = nullptr;
    s_nextLbl           = nullptr;
    s_progBar           = nullptr;
    s_progMetaRow       = nullptr;
    s_progDetailLbl     = nullptr;
    s_progPctLbl        = nullptr;
    s_cancelBtn         = nullptr;
    s_stepCount         = 0;
    s_stepIdx           = 0;
    s_progressOpen      = false;
    s_progressLocked    = false;
    s_progIndeterminate = false;
    s_progLastPct       = -1;
    s_onCancel          = nullptr;
}

bool m3ModalIsOpen() {
    return s_backdrop != nullptr;
}

bool m3ModalIsProgressOpen() {
    return s_backdrop != nullptr && s_progressOpen;
}

bool m3ModalIsLocked() {
    return s_backdrop != nullptr && s_progressLocked;
}
