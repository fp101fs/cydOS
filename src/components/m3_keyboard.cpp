#include "m3_keyboard.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../ui/app_manager.h"
#include <ctype.h>
#include <string.h>

static lv_obj_t* s_kbd = nullptr;
static lv_obj_t* s_preview = nullptr;
static lv_obj_t* s_previewLbl = nullptr;
static lv_obj_t* s_targetTa = nullptr;
static M3KbdEnterCb s_onEnter = nullptr;

static lv_obj_t* s_pressedBtn = nullptr;
static bool s_pressValid = false;

static constexpr int16_t PREVIEW_W          = 36;
static constexpr int16_t PREVIEW_H          = 42;
static constexpr int16_t PREVIEW_GAP        = 4;
static constexpr int16_t PREVIEW_MARGIN     = 2;
static constexpr int16_t PREVIEW_CANCEL_PAD = 4;

static bool s_shift = true;
static bool s_symMode = false;
static bool s_stylesInited = false;
static lv_style_t s_keyStyle;
static lv_style_t s_keyLblStyle;

static lv_obj_t* s_keyLbls[4][10] = {};
static uint8_t s_rowLens[4] = { 10, 9, 9, 6 };

static const char* s_alphaRows[3][10] = {
    { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
    { "A", "S", "D", "F", "G", "H", "J", "K", "L", nullptr },
    { LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, nullptr }
};

static const char* s_symRows[3][10] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "@", "#", "$", "_", "&", "-", "+", "(", ")", nullptr },
    { "ABC", "*", "\"", "'", ":", ";", "!", "?", LV_SYMBOL_BACKSPACE, nullptr }
};

static void updateKeyLabels() {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < s_rowLens[r]; ++c) {
            const char* txt = s_symMode ? s_symRows[r][c] : s_alphaRows[r][c];
            if (!txt || !s_keyLbls[r][c]) continue;

            if (!s_symMode && strlen(txt) == 1 && isalpha((unsigned char)txt[0])) {
                char ch[2] = { s_shift ? txt[0] : (char)tolower((unsigned char)txt[0]), '\0' };
                lv_label_set_text(s_keyLbls[r][c], ch);
            } else {
                lv_label_set_text(s_keyLbls[r][c], txt);
            }
        }
    }
    if (s_keyLbls[3][0]) {
        lv_label_set_text(s_keyLbls[3][0], s_symMode ? "ABC" : "123");
    }
}

static bool keyHasPreview(int r, int c) {
    if (r == 0 || r == 1) return true;
    if (r == 2) return (c >= 1 && c <= 7);
    if (r == 3) return (c == 2 || c == 4);
    return false;
}

static bool isPointOnKey(lv_obj_t* btn, const lv_point_t& pt) {
    lv_area_t coords;
    lv_obj_get_coords(btn, &coords);
    return pt.x >= (coords.x1 - PREVIEW_CANCEL_PAD) &&
           pt.x <= (coords.x2 + PREVIEW_CANCEL_PAD) &&
           pt.y >= (coords.y1 - PREVIEW_CANCEL_PAD) &&
           pt.y <= (coords.y2 + PREVIEW_CANCEL_PAD);
}

static void hideKeyPreview() {
    if (s_preview) {
        lv_obj_add_flag(s_preview, LV_OBJ_FLAG_HIDDEN);
    }
}

static void showKeyPreview(lv_obj_t* btn, int r, int c) {
    if (!s_preview || !s_previewLbl || !btn || !s_keyLbls[r][c]) return;
    const char* txt = lv_label_get_text(s_keyLbls[r][c]);
    if (!txt || !txt[0]) return;

    lv_obj_set_style_text_font(s_previewLbl,
                               strlen(txt) > 1 ? &lv_font_montserrat_20 : &lv_font_montserrat_24,
                               0);
    lv_label_set_text(s_previewLbl, txt);

    lv_area_t coords;
    lv_obj_get_coords(btn, &coords);

    int16_t keyCx = (int16_t)((coords.x1 + coords.x2 + 1) / 2);
    int16_t px = (int16_t)(keyCx - PREVIEW_W / 2);
    int16_t maxX = (int16_t)displayWidth() - PREVIEW_W - PREVIEW_MARGIN;
    if (px < PREVIEW_MARGIN) px = PREVIEW_MARGIN;
    if (px > maxX) px = maxX;

    int16_t py = (int16_t)(coords.y1 - PREVIEW_H - PREVIEW_GAP);
    int16_t minY = M3Dim::STATUS_BAR_H + PREVIEW_MARGIN;
    if (py < minY) py = minY;

    lv_obj_set_pos(s_preview, px, py);
    lv_obj_clear_flag(s_preview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_preview);
}

static void keyEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);
    uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    int r = (int)(packed >> 8);
    int c = (int)(packed & 0xFF);

    if (code == LV_EVENT_PRESSED) {
        s_pressedBtn = btn;
        s_pressValid = true;
        if (keyHasPreview(r, c)) {
            showKeyPreview(btn, r, c);
        }
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (s_pressedBtn != btn) return;
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        bool onKey = isPointOnKey(btn, pt);
        if (!onKey && s_pressValid) {
            s_pressValid = false;
            hideKeyPreview();
        } else if (onKey && !s_pressValid) {
            s_pressValid = true;
            if (keyHasPreview(r, c)) {
                showKeyPreview(btn, r, c);
            }
        }
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        if (s_pressedBtn == btn) {
            s_pressedBtn = nullptr;
            s_pressValid = false;
        }
        hideKeyPreview();
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        if (s_pressedBtn == btn) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t pt;
                lv_indev_get_point(indev, &pt);
                if (!isPointOnKey(btn, pt)) {
                    s_pressValid = false;
                }
            }
        }
        hideKeyPreview();
        return;
    }

    if (code != LV_EVENT_CLICKED) return;

    bool allow = true;
    if (s_pressedBtn == btn) {
        allow = s_pressValid;
        s_pressedBtn = nullptr;
        s_pressValid = false;
    }
    hideKeyPreview();
    if (!allow) return;

    m3PulseFeedback();

    if (r == 2 && c == 0) {
        if (s_symMode) s_symMode = false;
        else s_shift = !s_shift;
        updateKeyLabels();
        return;
    }
    if (r == 2 && c == 8) {
        if (s_targetTa) lv_textarea_del_char(s_targetTa);
        return;
    }
    if (r == 3) {
        if (c == 0) {
            s_symMode = !s_symMode;
            updateKeyLabels();
            return;
        } else if (c == 1) {
            m3KeyboardHide();
            appManagerLaunchById("settings");
            return;
        } else if (c == 2) {
            if (s_targetTa) lv_textarea_add_text(s_targetTa, ":)");
            return;
        } else if (c == 3) {
            if (s_targetTa) lv_textarea_add_char(s_targetTa, ' ');
            return;
        } else if (c == 4) {
            if (s_targetTa) lv_textarea_add_char(s_targetTa, '.');
            return;
        } else if (c == 5) {
            const char* txt = s_targetTa ? lv_textarea_get_text(s_targetTa) : "";
            M3KbdEnterCb cb = s_onEnter;
            m3KeyboardHide();
            if (cb) cb(txt);
            return;
        }
    }

    if (!s_targetTa || !s_keyLbls[r][c]) return;
    const char* ch = lv_label_get_text(s_keyLbls[r][c]);
    if (ch && ch[0]) {
        lv_textarea_add_text(s_targetTa, ch);
        if (!s_symMode && s_shift) {
            s_shift = false;
            updateKeyLabels();
        }
    }
}

static lv_obj_t* addKey(lv_obj_t* parent, int r, int c, int16_t x, int16_t y, int16_t w, int16_t h, const lv_font_t* font) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &s_keyStyle, 0);
    lv_obj_add_event_cb(btn, keyEventCb, LV_EVENT_ALL, (void*)(uintptr_t)((r << 8) | c));

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_add_style(lbl, &s_keyLblStyle, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_center(lbl);
    s_keyLbls[r][c] = lbl;
    return btn;
}

void m3KeyboardInit() {
    if (s_kbd) return;

    if (!s_stylesInited) {
        lv_style_init(&s_keyStyle);
        lv_style_set_radius(&s_keyStyle, 6);
        lv_style_set_shadow_width(&s_keyStyle, 0);
        lv_style_set_pad_all(&s_keyStyle, 0);
        lv_style_set_bg_opa(&s_keyStyle, LV_OPA_COVER);

        lv_style_init(&s_keyLblStyle);
        s_stylesInited = true;
    }

    uint16_t w = displayWidth();
    uint16_t h = displayHeight();
    int16_t kbdH = 138;
    int16_t kbdY = (int16_t)h - M3Dim::NAV_BAR_H - kbdH;

    s_kbd = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_kbd, w, kbdH);
    lv_obj_set_pos(s_kbd, 0, kbdY);
    lv_obj_set_style_border_width(s_kbd, 0, 0);
    lv_obj_set_style_radius(s_kbd, 0, 0);
    lv_obj_set_style_pad_all(s_kbd, 0, 0);
    lv_obj_clear_flag(s_kbd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);

    const int16_t rowY[4] = { 5, 38, 71, 104 };
    const int16_t keyH = 29;

    for (int c = 0; c < 10; ++c) {
        addKey(s_kbd, 0, c, 6 + c * 23, rowY[0], 21, keyH, &lv_font_montserrat_14);
    }
    for (int c = 0; c < 9; ++c) {
        addKey(s_kbd, 1, c, 14 + c * 24, rowY[1], 21, keyH, &lv_font_montserrat_14);
    }

    int16_t r2x = 6;
    for (int c = 0; c < 9; ++c) {
        int16_t kw = (c == 0 || c == 8) ? 31 : 21;
        addKey(s_kbd, 2, c, r2x, rowY[2], kw, keyH, (c == 0 || c == 8) ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
        r2x += kw + 2;
    }

    const int16_t r3w[6] = { 32, 26, 26, 78, 24, 34 };
    const char* r3txt[6] = { "123", LV_SYMBOL_SETTINGS, ":)", " ", ".", LV_SYMBOL_NEW_LINE };
    int16_t r3x = 5;
    for (int c = 0; c < 6; ++c) {
        addKey(s_kbd, 3, c, r3x, rowY[3], r3w[c], keyH, &lv_font_montserrat_12);
        lv_label_set_text(s_keyLbls[3][c], r3txt[c]);
        r3x += r3w[c] + 2;
    }

    s_preview = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_preview, PREVIEW_W, PREVIEW_H);
    lv_obj_set_style_radius(s_preview, 8, 0);
    lv_obj_set_style_border_width(s_preview, 1, 0);
    lv_obj_set_style_pad_all(s_preview, 0, 0);
    lv_obj_set_style_bg_opa(s_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_preview, 8, 0);
    lv_obj_set_style_shadow_opa(s_preview, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(s_preview, 2, 0);
    lv_obj_clear_flag(s_preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_preview, LV_OBJ_FLAG_HIDDEN);

    s_previewLbl = lv_label_create(s_preview);
    lv_obj_set_style_text_font(s_previewLbl, &lv_font_montserrat_24, 0);
    lv_obj_center(s_previewLbl);

    updateKeyLabels();
    m3KeyboardRefreshTheme();
}

void m3KeyboardRefreshTheme() {
    if (!s_kbd) return;
    uint32_t bgHex  = g_sys.lightMode ? 0xE2E5EC : 0x1B1C1E;
    uint32_t keyHex = g_sys.lightMode ? 0xFFFFFF : 0x35363A;
    uint32_t txtHex = g_sys.lightMode ? 0x1A1C20 : 0xE8EAED;

    lv_obj_set_style_bg_color(s_kbd, lv_color_hex(bgHex), 0);
    lv_style_set_bg_color(&s_keyStyle, lv_color_hex(keyHex));
    lv_style_set_text_color(&s_keyLblStyle, lv_color_hex(txtHex));
    lv_obj_report_style_change(&s_keyStyle);
    lv_obj_report_style_change(&s_keyLblStyle);

    if (s_preview) {
        uint32_t prevBg     = g_sys.lightMode ? 0xFFFFFF : 0x4A4D54;
        uint32_t prevBorder = m3PrimaryHex();
        uint32_t prevTxt    = g_sys.lightMode ? 0x1A1C20 : 0xFFFFFF;
        lv_obj_set_style_bg_color(s_preview, lv_color_hex(prevBg), 0);
        lv_obj_set_style_border_color(s_preview, lv_color_hex(prevBorder), 0);
        if (s_previewLbl) {
            lv_obj_set_style_text_color(s_previewLbl, lv_color_hex(prevTxt), 0);
        }
    }
}

static void taTapCb(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    M3KbdEnterCb cb = (M3KbdEnterCb)lv_event_get_user_data(e);
    m3KeyboardShow(ta, cb);
}

void m3KeyboardAttach(lv_obj_t* textarea, M3KbdEnterCb onEnter) {
    if (!textarea) return;
    lv_obj_add_event_cb(textarea, taTapCb, LV_EVENT_FOCUSED, (void*)onEnter);
    lv_obj_add_event_cb(textarea, taTapCb, LV_EVENT_CLICKED, (void*)onEnter);
}

void m3KeyboardShow(lv_obj_t* textarea, M3KbdEnterCb onEnter) {
    if (!s_kbd) m3KeyboardInit();
    s_targetTa = textarea;
    s_onEnter = onEnter;
    s_pressedBtn = nullptr;
    s_pressValid = false;
    hideKeyPreview();
    m3KeyboardRefreshTheme();
    lv_obj_clear_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kbd);
}

void m3KeyboardHide() {
    if (!s_kbd) return;
    s_pressedBtn = nullptr;
    s_pressValid = false;
    hideKeyPreview();
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    if (s_targetTa) {
        lv_obj_clear_state(s_targetTa, LV_STATE_FOCUSED);
    }
    s_targetTa = nullptr;
    s_onEnter = nullptr;
}

bool m3KeyboardIsOpen() {
    return s_kbd && !lv_obj_has_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
}
