#include "m3_modal.h"
#include "m3_widgets.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include <stdio.h>

static constexpr uint8_t MODAL_MAX_BTNS  = 4;
static constexpr uint8_t MODAL_MAX_STEPS = 12;

static lv_obj_t* s_backdrop = nullptr;
static lv_obj_t* s_card     = nullptr;
static lv_obj_t* s_titleLbl = nullptr;
static lv_obj_t* s_bodyLbl  = nullptr;
static lv_obj_t* s_pageLbl  = nullptr;
static lv_obj_t* s_prevBtn  = nullptr;
static lv_obj_t* s_nextBtn  = nullptr;
static lv_obj_t* s_nextLbl  = nullptr;

static M3ModalBtn  s_btns[MODAL_MAX_BTNS];
static M3ModalStep s_steps[MODAL_MAX_STEPS];
static uint8_t     s_stepCount = 0;
static uint8_t     s_stepIdx   = 0;
static uint32_t    s_accentHex = 0;

static void closeBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    m3ModalClose();
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

    lv_obj_t* xBtn = lv_btn_create(hdr);
    lv_obj_set_size(xBtn, 26, 26);
    lv_obj_set_style_radius(xBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(xBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
    lv_obj_set_style_bg_opa(xBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(xBtn, 0, 0);
    lv_obj_set_style_pad_all(xBtn, 0, 0);
    lv_obj_add_event_cb(xBtn, closeBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* xLbl = lv_label_create(xBtn);
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
    lv_obj_t* bodyWrap = lv_obj_create(s_card);
    lv_obj_set_width(bodyWrap, LV_PCT(100));
    lv_obj_set_flex_grow(bodyWrap, 1);
    lv_obj_set_style_bg_opa(bodyWrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bodyWrap, 0, 0);
    lv_obj_set_style_pad_hor(bodyWrap, 2, 0);
    lv_obj_set_style_pad_ver(bodyWrap, 0, 0);
    lv_obj_set_flex_flow(bodyWrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bodyWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(bodyWrap, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(bodyWrap, LV_SCROLLBAR_MODE_AUTO);

    s_bodyLbl = lv_label_create(bodyWrap);
    lv_obj_set_width(s_bodyLbl, LV_PCT(100));
    lv_label_set_long_mode(s_bodyLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_bodyLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_bodyLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_line_space(s_bodyLbl, 4, 0);

    // Footer container
    lv_obj_t* footer = lv_obj_create(s_card);
    lv_obj_set_size(footer, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(footer, 6, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    return footer;
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

    if (!buttons || buttonCount == 0) {
        lv_obj_t* okBtn = m3PillBtn(footer, "OK", s_accentHex, 0xFFFFFF, LV_PCT(100), 30);
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

void m3ModalClose() {
    if (s_backdrop) {
        lv_obj_del(s_backdrop);
        s_backdrop = nullptr;
    }
    s_card      = nullptr;
    s_titleLbl  = nullptr;
    s_bodyLbl   = nullptr;
    s_pageLbl   = nullptr;
    s_prevBtn   = nullptr;
    s_nextBtn   = nullptr;
    s_nextLbl   = nullptr;
    s_stepCount = 0;
    s_stepIdx   = 0;
}

bool m3ModalIsOpen() {
    return s_backdrop != nullptr;
}
