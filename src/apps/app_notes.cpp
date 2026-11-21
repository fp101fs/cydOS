#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <string.h>

static char s_noteText[160] = "Pixel 8 CYD Notes:\n- 240x320 Portrait LVGL\n- Swipe top for Quick Settings\n- Tap here to edit!";
static lv_obj_t* s_ta = nullptr;
static lv_obj_t* s_kb = nullptr;

static void taFocusCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED && s_kb) {
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_VALUE_CHANGED && s_ta) {
        strncpy(s_noteText, lv_textarea_get_text(s_ta), sizeof(s_noteText) - 1);
        s_noteText[sizeof(s_noteText) - 1] = '\0';
    }
}

static void kbReadyCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (s_ta) {
            strncpy(s_noteText, lv_textarea_get_text(s_ta), sizeof(s_noteText) - 1);
            s_noteText[sizeof(s_noteText) - 1] = '\0';
        }
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_ta, LV_STATE_FOCUSED);
        m3ShowToast("Note saved", 1000);
    }
}

void buildAppNotes(lv_obj_t* body) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_ta = lv_textarea_create(body);
    lv_obj_set_size(s_ta, LV_PCT(100), 108);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_text(s_ta, s_noteText);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(M3Color::SURFACE_HIGH), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(s_ta, lv_color_hex(M3Color::AMBER), 0);
    lv_obj_set_style_border_width(s_ta, 1, 0);
    lv_obj_set_style_border_opa(s_ta, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_ta, 14, 0);
    lv_obj_add_event_cb(s_ta, taFocusCb, LV_EVENT_ALL, nullptr);

    s_kb = lv_keyboard_create(body);
    lv_obj_set_size(s_kb, LV_PCT(100), 102);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_ta);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kbReadyCb, LV_EVENT_ALL, nullptr);
}
