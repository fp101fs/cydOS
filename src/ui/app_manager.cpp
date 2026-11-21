#include "app_manager.h"
#include "nav_bar.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"

static lv_obj_t* s_appWin = nullptr;
static lv_obj_t* s_appHeader = nullptr;
static const AppMeta* s_activeMeta = nullptr;
static bool s_closing = false;

static void animYCb(void* var, int32_t v) {
    lv_obj_set_y((lv_obj_t*)var, v);
}

static void closeAnimDoneCb(lv_anim_t* a) {
    (void)a;
    if (s_appWin) {
        lv_obj_del(s_appWin);
        s_appWin = nullptr;
    }
    s_appHeader = nullptr;
    s_activeMeta = nullptr;
    s_closing = false;
}

static void headerBackCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    appManagerClose();
}

void appManagerInit() {
    s_appWin = nullptr;
    s_appHeader = nullptr;
    s_activeMeta = nullptr;
    s_closing = false;
}

void appManagerLaunch(const AppMeta* meta) {
    if (!meta) return;
    if (!meta->buildFn) {
        if (meta->actionFn) meta->actionFn();
        return;
    }

    if (m3KeyboardIsOpen()) m3KeyboardHide();

    if (s_appWin) {
        lv_anim_del(s_appWin, animYCb);
        lv_obj_del(s_appWin);
        s_appWin = nullptr;
    }
    s_appHeader = nullptr;
    s_closing = false;
    s_activeMeta = meta;

    uint16_t w = displayWidth();
    uint16_t h = displayHeight();
    uint16_t winH = h - M3Dim::STATUS_BAR_H - M3Dim::NAV_BAR_H;

    s_appWin = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_appWin, w, winH);
    lv_obj_set_pos(s_appWin, 0, h - M3Dim::NAV_BAR_H);
    lv_obj_set_style_bg_color(s_appWin, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_opa(s_appWin, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_appWin, 0, 0);
    lv_obj_set_style_radius(s_appWin, 0, 0);
    lv_obj_set_style_pad_all(s_appWin, 0, 0);
    lv_obj_clear_flag(s_appWin, LV_OBJ_FLAG_SCROLLABLE);

    s_appHeader = m3AppHeader(s_appWin, meta->name, meta->accentHex, headerBackCb);

    uint16_t bodyH = winH - M3Dim::APP_TOP_H;
    lv_obj_t* body = lv_obj_create(s_appWin);
    lv_obj_set_size(body, w, bodyH);
    lv_obj_set_pos(body, 0, M3Dim::APP_TOP_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, M3Dim::PAD_MD, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    meta->buildFn(body);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_appWin);
    lv_anim_set_values(&a, h - M3Dim::NAV_BAR_H, M3Dim::STATUS_BAR_H);
    lv_anim_set_time(&a, 175);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_start(&a);
}

void appManagerLaunchById(const char* id) {
    appManagerLaunch(appFindById(id));
}

void appManagerClose() {
    if (!s_appWin || s_closing) return;
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    s_closing = true;
    s_activeMeta = nullptr;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_appWin);
    lv_anim_set_values(&a, lv_obj_get_y(s_appWin), displayHeight() - M3Dim::NAV_BAR_H);
    lv_anim_set_time(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_set_ready_cb(&a, closeAnimDoneCb);
    lv_anim_start(&a);
}

bool appManagerHasActiveApp() {
    return s_appWin != nullptr && !s_closing;
}

void appManagerRefreshTheme() {
    if (!s_appWin) return;
    lv_obj_set_style_bg_color(s_appWin, lv_color_hex(m3CurrentBgHex()), 0);
    if (s_appHeader) {
        m3AppHeaderRefreshTheme(s_appHeader);
    }
    if (s_activeMeta && s_activeMeta->themeFn && !s_closing) {
        s_activeMeta->themeFn();
    }
}

void appManagerLoop() {
    if (s_activeMeta && s_activeMeta->tickFn && !s_closing) {
        s_activeMeta->tickFn();
    }
}
