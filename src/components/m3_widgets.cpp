#include "m3_widgets.h"
#include "../theme/m3_theme.h"

lv_obj_t* m3Card(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(M3Color::OUTLINE), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_radius(card, radius, 0);
    lv_obj_set_style_pad_all(card, M3Dim::PAD_MD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t* m3AppHeader(lv_obj_t* parent, const char* title, uint32_t accentHex, lv_event_cb_t backCb, void* userData) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), M3Dim::APP_TOP_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(M3Color::SURFACE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = lv_btn_create(bar);
    lv_obj_set_size(backBtn, 32, 32);
    lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(backBtn, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(backBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(backBtn, 0, 0);
    if (backCb) lv_obj_add_event_cb(backBtn, backCb, LV_EVENT_CLICKED, userData);

    lv_obj_t* backIco = lv_label_create(backBtn);
    lv_label_set_text(backIco, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backIco, lv_color_hex(accentHex), 0);
    lv_obj_set_style_text_font(backIco, &lv_font_montserrat_14, 0);
    lv_obj_center(backIco);

    lv_obj_t* lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);

    // Subtle bottom accent hairline
    lv_obj_t* line = lv_obj_create(bar);
    lv_obj_set_size(line, LV_PCT(100), 1);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(M3Color::OUTLINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    return bar;
}

lv_obj_t* m3ToggleRow(
    lv_obj_t* parent,
    const char* iconSym,
    const char* title,
    const char* subtitle,
    bool initialState,
    uint32_t accentHex,
    lv_event_cb_t onChangeCb,
    void* userData
) {
    lv_obj_t* row = m3Card(parent, LV_PCT(100), subtitle ? 52 : 44, 14);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);

    if (iconSym && iconSym[0]) {
        lv_obj_t* ico = lv_label_create(row);
        lv_label_set_text(ico, iconSym);
        lv_obj_set_style_text_color(ico, lv_color_hex(accentHex), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 2, 0);
    }

    int16_t textX = (iconSym && iconSym[0]) ? 24 : 2;
    lv_obj_t* tLbl = lv_label_create(row);
    lv_label_set_text(tLbl, title);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_14, 0);
    if (subtitle) {
        lv_obj_align(tLbl, LV_ALIGN_TOP_LEFT, textX, 1);
        lv_obj_t* sLbl = lv_label_create(row);
        lv_label_set_text(sLbl, subtitle);
        lv_obj_set_style_text_color(sLbl, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(sLbl, LV_ALIGN_BOTTOM_LEFT, textX, -1);
    } else {
        lv_obj_align(tLbl, LV_ALIGN_LEFT_MID, textX, 0);
    }

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 40, 22);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(M3Color::SURFACE_HIGHEST), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(accentHex), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(M3Color::TEXT_PRIMARY), LV_PART_KNOB);
    if (initialState) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (onChangeCb) lv_obj_add_event_cb(sw, onChangeCb, LV_EVENT_VALUE_CHANGED, userData);

    return row;
}

lv_obj_t* m3InfoRow(lv_obj_t* parent, const char* label, const char* value, lv_obj_t** outValueLabel) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* l = lv_label_create(row);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* v = lv_label_create(row);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_color(v, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_12, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);

    if (outValueLabel) *outValueLabel = v;
    return row;
}

lv_obj_t* m3PillBtn(lv_obj_t* parent, const char* text, uint32_t bgHex, uint32_t fgHex, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fgHex), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t* s_toast = nullptr;
static lv_timer_t* s_toastTimer = nullptr;

static void toastTimerCb(lv_timer_t* t) {
    (void)t;
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = nullptr;
    }
    if (s_toastTimer) {
        lv_timer_del(s_toastTimer);
        s_toastTimer = nullptr;
    }
}

void m3ShowToast(const char* text, uint32_t durationMs) {
    if (s_toast) {
        lv_obj_del(s_toast);
        s_toast = nullptr;
    }
    if (s_toastTimer) {
        lv_timer_del(s_toastTimer);
        s_toastTimer = nullptr;
    }

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, 216, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(M3Color::SURFACE_HIGHEST), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_border_opa(s_toast, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_toast, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(s_toast, 14, 0);
    lv_obj_set_style_pad_ver(s_toast, 7, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t* lbl = lv_label_create(s_toast);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);

    s_toastTimer = lv_timer_create(toastTimerCb, durationMs, nullptr);
    lv_timer_set_repeat_count(s_toastTimer, 1);
}
