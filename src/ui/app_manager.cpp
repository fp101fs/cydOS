#include "app_manager.h"
#include "home_screen.h"
#include "../theme/m3_theme.h"
#include "../drivers/display_drv.h"
#include "../components/m3_widgets.h"

static lv_obj_t* s_appWin = nullptr;
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
    s_activeMeta = nullptr;
    s_closing = false;
}

static void navBackCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerClose();
}

void appManagerInit() {
    s_appWin = nullptr;
    s_activeMeta = nullptr;
    s_closing = false;
}

void appManagerLaunch(AppId id) {
    const AppMeta* meta = appGetMeta(id);
    if (!meta) return;

    // Replace any currently open app cleanly
    if (s_appWin) {
        lv_anim_del(s_appWin, animYCb);
        lv_obj_del(s_appWin);
        s_appWin = nullptr;
    }
    s_closing = false;
    s_activeMeta = meta;

    uint16_t w = displayWidth();
    uint16_t h = displayHeight();
    uint16_t winH = h - M3Dim::STATUS_BAR_H;

    s_appWin = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_appWin, w, winH);
    lv_obj_set_pos(s_appWin, 0, h);  // Start off-screen at bottom for smooth slide-up
    lv_obj_set_style_bg_color(s_appWin, lv_color_hex(m3CurrentBgHex()), 0);
    lv_obj_set_style_bg_opa(s_appWin, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_appWin, 0, 0);
    lv_obj_set_style_radius(s_appWin, 0, 0);
    lv_obj_set_style_pad_all(s_appWin, 0, 0);
    lv_obj_clear_flag(s_appWin, LV_OBJ_FLAG_SCROLLABLE);

    // Top App Header
    m3AppHeader(s_appWin, meta->name, meta->accentHex, navBackCb);

    // Scrollable App Body
    uint16_t bodyH = winH - M3Dim::APP_TOP_H - M3Dim::NAV_BAR_H;
    lv_obj_t* body = lv_obj_create(s_appWin);
    lv_obj_set_size(body, w, bodyH);
    lv_obj_set_pos(body, 0, M3Dim::APP_TOP_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, M3Dim::PAD_MD, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    if (meta->buildFn) {
        meta->buildFn(body);
    }

    // Bottom Android Gesture / Navigation Bar
    lv_obj_t* navBar = lv_obj_create(s_appWin);
    lv_obj_set_size(navBar, w, M3Dim::NAV_BAR_H);
    lv_obj_align(navBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(navBar, lv_color_hex(M3Color::SURFACE), 0);
    lv_obj_set_style_bg_opa(navBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(navBar, 0, 0);
    lv_obj_set_style_radius(navBar, 0, 0);
    lv_obj_set_style_pad_all(navBar, 0, 0);
    lv_obj_clear_flag(navBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(navBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(navBar, navBackCb, LV_EVENT_CLICKED, nullptr);

    // Gesture pill in center
    lv_obj_t* pill = lv_obj_create(navBar);
    lv_obj_set_size(pill, 68, 4);
    lv_obj_center(pill);
    lv_obj_set_style_bg_color(pill, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_70, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, M3Dim::RADIUS_PILL, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);

    // Back arrow hint on left
    lv_obj_t* backHint = lv_label_create(navBar);
    lv_label_set_text(backHint, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(backHint, lv_color_hex(M3Color::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(backHint, &lv_font_montserrat_10, 0);
    lv_obj_align(backHint, LV_ALIGN_LEFT_MID, 18, 0);

    // Slide-up animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_appWin);
    lv_anim_set_values(&a, h, M3Dim::STATUS_BAR_H);
    lv_anim_set_time(&a, 175);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_start(&a);
}

void appManagerClose() {
    if (!s_appWin || s_closing) return;
    s_closing = true;
    s_activeMeta = nullptr;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_appWin);
    lv_anim_set_values(&a, lv_obj_get_y(s_appWin), displayHeight());
    lv_anim_set_time(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, animYCb);
    lv_anim_set_ready_cb(&a, closeAnimDoneCb);
    lv_anim_start(&a);
}

bool appManagerHasActiveApp() {
    return s_appWin != nullptr && !s_closing;
}

void appManagerLoop() {
    if (s_activeMeta && s_activeMeta->tickFn && !s_closing) {
        s_activeMeta->tickFn();
    }
}
