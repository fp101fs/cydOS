#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../drivers/companion_mgr.h"
#include "../drivers/screen_mirror.h"
#include "../ui/app_manager.h"
#include <stdio.h>

// ============================================================================
// Remote Screen Mirror & Control Companion App (Standalone CYD App)
// ============================================================================

static lv_obj_t* s_body         = nullptr;
static lv_obj_t* s_statusDot    = nullptr;
static lv_obj_t* s_statusTitle  = nullptr;
static lv_obj_t* s_statusSub    = nullptr;
static lv_obj_t* s_streamSw     = nullptr;
static lv_obj_t* s_remoteSw     = nullptr;
static lv_obj_t* s_statsLbl     = nullptr;

static bool     s_prevConn      = false;
static bool     s_prevStream    = false;
static bool     s_prevRemote    = false;
static uint32_t s_lastStatsMs   = 0;

static void updateUi() {
    if (!s_body) return;

    bool conn   = companionIsConnected();
    bool stream = screenMirrorIsStreaming();
    bool remote = screenMirrorIsRemoteControlEnabled();

    if (s_statusDot) {
        uint32_t col = (conn && stream) ? M3Color::TERTIARY :
                       conn             ? M3Color::PRIMARY :
                                          M3Color::AMBER;
        lv_obj_set_style_bg_color(s_statusDot, lv_color_hex(col), 0);
    }

    if (s_statusTitle) {
        if (!conn) lv_label_set_text(s_statusTitle, "Phone Not Linked");
        else if (stream) lv_label_set_text(s_statusTitle, "Mirroring Live");
        else lv_label_set_text(s_statusTitle, "Companion Ready");
    }

    if (s_statusSub) {
        char host[COMPANION_HOST_MAX] = "";
        companionGetHost(host, sizeof(host));
        char sub[64];
        if (!conn) {
            snprintf(sub, sizeof(sub), "Link via Android Link app first");
        } else if (stream) {
            snprintf(sub, sizeof(sub), "%s  •  %u FPS", host[0] ? host : "Phone", (unsigned)screenMirrorGetFps());
        } else {
            snprintf(sub, sizeof(sub), "Linked to %s", host[0] ? host : "Android");
        }
        lv_label_set_text(s_statusSub, sub);
    }

    if (s_streamSw) {
        if (stream) lv_obj_add_state(s_streamSw, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_streamSw, LV_STATE_CHECKED);
    }
    if (s_remoteSw) {
        if (remote) lv_obj_add_state(s_remoteSw, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_remoteSw, LV_STATE_CHECKED);
    }

    if (s_statsLbl) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Bands: %lu   •   Remote cmds: %lu",
                 (unsigned long)screenMirrorGetBandsSent(),
                 (unsigned long)screenMirrorGetRemoteInputsCount());
        lv_label_set_text(s_statsLbl, buf);
    }
}

static void streamSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    if (en && !companionIsConnected()) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        m3ShowToast("Connect Android Companion first", 1400);
        return;
    }
    if (!screenMirrorSetStreaming(en)) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        m3ShowToast("Low memory for mirror", 1400);
        return;
    }
    m3ShowToast(en ? "Screen mirror started" : "Screen mirror stopped", 1100);
    updateUi();
}

static void remoteSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    screenMirrorSetRemoteControl(en);
    m3ShowToast(en ? "Remote touch enabled" : "Remote touch locked", 1100);
    updateUi();
}

static void syncBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (!companionIsConnected()) {
        appManagerLaunchById("android_link");
        return;
    }
    if (!screenMirrorIsStreaming()) {
        screenMirrorSetStreaming(true);
    } else {
        screenMirrorForceFullSync();
    }
    m3ShowToast("Synced screen & UI map", 1000);
    updateUi();
}

static void refreshAppMirrorTheme() {
    if (!s_body) return;
    updateUi();
}

static void mirrorDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    s_statusDot = nullptr;
    s_statusTitle = nullptr;
    s_statusSub = nullptr;
    s_streamSw = nullptr;
    s_remoteSw = nullptr;
    s_statsLbl = nullptr;
}

static void buildAppMirror(lv_obj_t* body) {
    s_body = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, mirrorDeleteCb, LV_EVENT_DELETE, nullptr);

    // Status Card
    lv_obj_t* statusCard = m3Card(body, LV_PCT(100), 48, 14);
    lv_obj_set_style_pad_hor(statusCard, 10, 0);
    lv_obj_set_style_pad_ver(statusCard, 6, 0);

    s_statusDot = lv_obj_create(statusCard);
    lv_obj_set_size(s_statusDot, 10, 10);
    lv_obj_set_style_radius(s_statusDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_statusDot, 0, 0);
    lv_obj_align(s_statusDot, LV_ALIGN_LEFT_MID, 0, 0);

    s_statusTitle = lv_label_create(statusCard);
    lv_label_set_text(s_statusTitle, "Companion Ready");
    lv_obj_set_style_text_color(s_statusTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(s_statusTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(s_statusTitle, LV_ALIGN_TOP_LEFT, 18, 0);

    s_statusSub = lv_label_create(statusCard);
    lv_label_set_text(s_statusSub, "");
    lv_obj_set_style_text_color(s_statusSub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(s_statusSub, &lv_font_montserrat_10, 0);
    lv_obj_set_width(s_statusSub, 178);
    lv_label_set_long_mode(s_statusSub, LV_LABEL_LONG_DOT);
    lv_obj_align(s_statusSub, LV_ALIGN_BOTTOM_LEFT, 18, 0);

    // Toggle: Live Screen Mirror
    lv_obj_t* r1 = m3ToggleRow(body, LV_SYMBOL_EYE_OPEN, "Screen Mirror", "Stream to Android app",
                               screenMirrorIsStreaming(), M3Color::TERTIARY, streamSwCb);
    s_streamSw = lv_obj_get_child(r1, 3);

    // Toggle: Remote Touch & API Control
    lv_obj_t* r2 = m3ToggleRow(body, LV_SYMBOL_EDIT, "Remote Control", "Allow taps & API cmds",
                               screenMirrorIsRemoteControlEnabled(), M3Color::PRIMARY, remoteSwCb);
    s_remoteSw = lv_obj_get_child(r2, 3);

    // Action Pill
    lv_obj_t* syncBtn = m3PillBtn(body, LV_SYMBOL_REFRESH "  Sync Frame & UI Map",
                                  M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 28);
    lv_obj_add_event_cb(syncBtn, syncBtnCb, LV_EVENT_CLICKED, nullptr);

    // Telemetry line
    s_statsLbl = lv_label_create(body);
    lv_label_set_text(s_statsLbl, "Bands: 0   •   Remote cmds: 0");
    lv_obj_set_style_text_color(s_statsLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(s_statsLbl, &lv_font_montserrat_10, 0);

    s_prevConn = companionIsConnected();
    s_prevStream = screenMirrorIsStreaming();
    s_prevRemote = screenMirrorIsRemoteControlEnabled();
    s_lastStatsMs = millis();
    updateUi();
}

static void tickAppMirror() {
    if (!s_body) return;
    bool conn   = companionIsConnected();
    bool stream = screenMirrorIsStreaming();
    bool remote = screenMirrorIsRemoteControlEnabled();
    uint32_t now = millis();

    if (conn != s_prevConn || stream != s_prevStream || remote != s_prevRemote ||
        (stream && (uint32_t)(now - s_lastStatsMs) >= 750)) {
        s_prevConn = conn;
        s_prevStream = stream;
        s_prevRemote = remote;
        s_lastStatsMs = now;
        updateUi();
    }
}

REGISTER_APP_THEMED(mirror, "mirror", "Mirror", LV_SYMBOL_EYE_OPEN, 0x81C995, 0x1B4332, buildAppMirror, tickAppMirror, refreshAppMirrorTheme, 56);
