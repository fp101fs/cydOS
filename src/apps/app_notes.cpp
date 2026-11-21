#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include <string.h>

static char s_noteText[160] = "Pixel 8 CYD Notes:\n- 240x320 Portrait LVGL\n- Tap here to edit!";
static lv_obj_t* s_ta = nullptr;

static void onNoteEnter(const char* txt) {
    if (txt) {
        strncpy(s_noteText, txt, sizeof(s_noteText) - 1);
        s_noteText[sizeof(s_noteText) - 1] = '\0';
    }
    m3ShowToast("Note saved", 1000);
}

static void taChangeCb(lv_event_t* e) {
    (void)e;
    if (s_ta) {
        strncpy(s_noteText, lv_textarea_get_text(s_ta), sizeof(s_noteText) - 1);
        s_noteText[sizeof(s_noteText) - 1] = '\0';
    }
}

void buildAppNotes(lv_obj_t* body) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    s_ta = lv_textarea_create(body);
    lv_obj_set_size(s_ta, LV_PCT(100), 84);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_text(s_ta, s_noteText);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(s_ta, lv_color_hex(M3Color::AMBER), 0);
    lv_obj_set_style_border_width(s_ta, 1, 0);
    lv_obj_set_style_border_opa(s_ta, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_ta, 14, 0);
    lv_obj_add_event_cb(s_ta, taChangeCb, LV_EVENT_VALUE_CHANGED, nullptr);
    m3KeyboardAttach(s_ta, onNoteEnter);
}
