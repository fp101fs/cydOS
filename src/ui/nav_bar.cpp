#include "nav_bar.h"
#include "app_manager.h"
#include "home_screen.h"
#include "quick_settings.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_keyboard.h"
#include "../components/m3_modal.h"
#include "../components/m3_widgets.h"

static constexpr int16_t MENU_POPUP_W       = 184;
static constexpr int16_t MENU_POPUP_H       = 226;
static constexpr int16_t MENU_POPUP_MARGIN  = 6;
static constexpr int16_t MENU_POPUP_PAD_HOR = 8;
static constexpr int16_t MENU_POPUP_PAD_VER = 7;
static constexpr int16_t MENU_POPUP_GAP     = 5;
static constexpr int16_t MENU_ROW_COUNT     = 5;
static constexpr int16_t MENU_ROW_H         = 38;

static lv_obj_t* s_bar = nullptr;
static lv_obj_t* s_backLbl = nullptr;
static lv_obj_t* s_homeLbl = nullptr;
static lv_obj_t* s_menuBars[3] = {};
static lv_obj_t* s_menuPopup = nullptr;

static void closeMenu() {
    if (s_menuPopup) lv_obj_add_flag(s_menuPopup, LV_OBJ_FLAG_HIDDEN);
}

void navBarCloseMenu() {
    closeMenu();
}

static void backBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3ModalIsLocked()) return;
    if (m3ModalIsOpen()) {
        m3ModalClose();
        return;
    }
    if (s_menuPopup && !lv_obj_has_flag(s_menuPopup, LV_OBJ_FLAG_HIDDEN)) {
        closeMenu();
        return;
    }
    if (m3KeyboardIsOpen()) {
        m3KeyboardHide();
        return;
    }
    if (quickSettingsIsOpen()) {
        quickSettingsClose();
        return;
    }
    if (appManagerHasActiveApp()) {
        appManagerClose();
        return;
    }
    homeScreenGoPage(0);
}

static void homeBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3ModalIsLocked()) return;
    if (m3ModalIsOpen()) m3ModalClose();
    closeMenu();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    if (quickSettingsIsOpen()) quickSettingsClose();
    if (appManagerHasActiveApp()) appManagerClose();
    homeScreenGoPage(0);
}

static void menuItemCb(lv_event_t* e) {
    int act = (int)(intptr_t)lv_event_get_user_data(e);
    m3PulseFeedback();
    closeMenu();
    if (act == 0) {
        quickSettingsOpen();
    } else if (act == 1) {
        g_sys.lightMode = !g_sys.lightMode;
        m3ApplyThemeAll();
        m3ShowToast(g_sys.lightMode ? "Light Mode" : "Dark Mode", 1100);
    } else if (act == 2) {
        appManagerLaunchById("settings");
    } else if (act == 3) {
        appManagerLaunchById("sysinfo");
    } else if (act == 4) {
        if (appManagerHasActiveApp()) appManagerClose();
    }
}

static void menuBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3ModalIsLocked()) return;
    if (!s_menuPopup) return;
    if (lv_obj_has_flag(s_menuPopup, LV_OBJ_FLAG_HIDDEN)) {
        navBarRefreshTheme();
        lv_obj_clear_flag(s_menuPopup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_menuPopup);
    } else {
        closeMenu();
    }
}

static lv_obj_t* createNavSlot(lv_obj_t* parent, int16_t x, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, 2);
    lv_obj_set_size(btn, 72, 34);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(M3Color::PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

void navBarInit() {
    uint16_t w = displayWidth();
    uint16_t h = displayHeight();

    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, w, M3Dim::NAV_BAR_H);
    lv_obj_set_pos(s_bar, 0, h - M3Dim::NAV_BAR_H);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 0, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    // 3 evenly spaced Android buttons: Back | Home | Menu
    lv_obj_t* bBack = createNavSlot(s_bar, 8, backBtnCb);
    s_backLbl = lv_label_create(bBack);
    lv_label_set_text(s_backLbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_backLbl, &lv_font_montserrat_20, 0);
    lv_obj_center(s_backLbl);

    lv_obj_t* bHome = createNavSlot(s_bar, 84, homeBtnCb);
    s_homeLbl = lv_label_create(bHome);
    lv_label_set_text(s_homeLbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(s_homeLbl, &lv_font_montserrat_20, 0);
    lv_obj_center(s_homeLbl);

    lv_obj_t* bMenu = createNavSlot(s_bar, 160, menuBtnCb);
    for (int i = 0; i < 3; ++i) {
        s_menuBars[i] = lv_obj_create(bMenu);
        lv_obj_set_size(s_menuBars[i], 18, 2);
        lv_obj_align(s_menuBars[i], LV_ALIGN_CENTER, 0, (i - 1) * 6);
        lv_obj_set_style_border_width(s_menuBars[i], 0, 0);
        lv_obj_set_style_radius(s_menuBars[i], 1, 0);
        lv_obj_clear_flag(s_menuBars[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Sleek Menu Popup above the bottom bar
    s_menuPopup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_menuPopup, MENU_POPUP_W, MENU_POPUP_H);
    lv_obj_set_pos(s_menuPopup, w - MENU_POPUP_W - 8, h - M3Dim::NAV_BAR_H - MENU_POPUP_H - MENU_POPUP_MARGIN);
    lv_obj_set_style_radius(s_menuPopup, 16, 0);
    lv_obj_set_style_border_width(s_menuPopup, 1, 0);
    lv_obj_set_style_pad_hor(s_menuPopup, MENU_POPUP_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(s_menuPopup, MENU_POPUP_PAD_VER, 0);
    lv_obj_set_flex_flow(s_menuPopup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_menuPopup, MENU_POPUP_GAP, 0);
    lv_obj_clear_flag(s_menuPopup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_menuPopup, LV_OBJ_FLAG_HIDDEN);

    const char* items[MENU_ROW_COUNT] = {
        LV_SYMBOL_DOWN "  Quick Settings",
        LV_SYMBOL_EYE_OPEN "  Toggle Theme",
        LV_SYMBOL_SETTINGS "  Settings",
        LV_SYMBOL_CHARGE "  System Info",
        LV_SYMBOL_CLOSE "  Close App"
    };
    for (int i = 0; i < MENU_ROW_COUNT; ++i) {
        lv_obj_t* row = lv_btn_create(s_menuPopup);
        lv_obj_set_size(row, LV_PCT(100), MENU_ROW_H);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, menuItemCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 2, 0);
    }

    navBarRefreshTheme();
}

void navBarRefreshTheme() {
    if (!s_bar) return;

    lv_color_t fg = lv_color_hex(m3TextPrimaryHex());
    lv_obj_set_style_text_color(s_backLbl, fg, 0);
    lv_obj_set_style_text_color(s_homeLbl, fg, 0);
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_bg_color(s_menuBars[i], fg, 0);
    }

    if (s_menuPopup) {
        lv_obj_set_style_bg_color(s_menuPopup, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_menuPopup, lv_color_hex(m3OutlineHex()), 0);
        uint32_t cnt = lv_obj_get_child_cnt(s_menuPopup);
        for (uint32_t i = 0; i < cnt; ++i) {
            lv_obj_t* row = lv_obj_get_child(s_menuPopup, i);
            lv_obj_set_style_bg_color(row, lv_color_hex(m3SurfaceHighestHex()), 0);
            lv_obj_t* lbl = lv_obj_get_child(row, 0);
            if (lbl) lv_obj_set_style_text_color(lbl, fg, 0);
        }
    }
}

void navBarBringToFront() {
    if (s_bar) lv_obj_move_foreground(s_bar);
}
