#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include "../drivers/wifi_mgr.h"
#include "../ui/status_bar.h"
#include "../ui/quick_settings.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_body = nullptr;
static lv_obj_t* s_toggleRow = nullptr;
static lv_obj_t* s_toggleSub = nullptr;
static lv_obj_t* s_toggleSw = nullptr;
static lv_obj_t* s_scanBtn = nullptr;
static lv_obj_t* s_scanBtnLbl = nullptr;
static lv_obj_t* s_forgetBtn = nullptr;

static lv_obj_t* s_passCard = nullptr;
static lv_obj_t* s_passTitle = nullptr;
static lv_obj_t* s_passTa = nullptr;
static lv_obj_t* s_passCancelBtn = nullptr;

static lv_obj_t* s_listBox = nullptr;

static char s_targetSsid[WIFI_SSID_MAX_LEN + 1] = "";
static uint32_t s_lastScanGen = 0xFFFFFFFFUL;
static bool s_lastScanning = false;
static bool s_lastEnabled = false;
static bool s_lastConnected = false;
static bool s_lastConnecting = false;
static bool s_lastHasSaved = false;
static char s_lastIp[16] = "";

static void updateHeaderAndControls();
static void rebuildNetworkList();

static void hidePasswordPanel() {
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    if (s_passCard) lv_obj_add_flag(s_passCard, LV_OBJ_FLAG_HIDDEN);
    s_targetSsid[0] = '\0';
}

static void submitPassword(const char* pass) {
    if (!s_targetSsid[0]) return;
    char ssidCopy[WIFI_SSID_MAX_LEN + 1];
    strncpy(ssidCopy, s_targetSsid, sizeof(ssidCopy) - 1);
    ssidCopy[sizeof(ssidCopy) - 1] = '\0';

    hidePasswordPanel();
    wifiMgrConnect(ssidCopy, pass ? pass : "", true);
    updateHeaderAndControls();
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();

    char msg[56];
    snprintf(msg, sizeof(msg), "Connecting to %s...", ssidCopy);
    m3ShowToast(msg, 1400);
}

static void onPassEnter(const char* text) {
    submitPassword(text);
}

static void passConnectBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    const char* txt = s_passTa ? lv_textarea_get_text(s_passTa) : "";
    submitPassword(txt);
}

static void passCancelBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    hidePasswordPanel();
}

static void showPasswordPanel(const char* ssid) {
    if (!s_passCard || !s_passTa || !ssid || !ssid[0]) return;
    strncpy(s_targetSsid, ssid, sizeof(s_targetSsid) - 1);
    s_targetSsid[sizeof(s_targetSsid) - 1] = '\0';

    char title[56];
    snprintf(title, sizeof(title), LV_SYMBOL_EYE_CLOSE "  %s", s_targetSsid);
    if (s_passTitle) lv_label_set_text(s_passTitle, title);

    if (strcmp(s_targetSsid, wifiMgrGetSavedSsid()) == 0) {
        lv_textarea_set_text(s_passTa, wifiMgrGetSavedPass());
    } else {
        lv_textarea_set_text(s_passTa, "");
    }

    lv_obj_clear_flag(s_passCard, LV_OBJ_FLAG_HIDDEN);
    m3KeyboardShow(s_passTa, onPassEnter);
}

static void wifiSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    hidePasswordPanel();
    wifiMgrSetEnabled(en);
    if (en) {
        wifiMgrStartScan();
    }
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
    updateHeaderAndControls();
    rebuildNetworkList();
}

static void scanBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (!g_sys.wifiEnabled || g_sys.airplaneMode) {
        m3ShowToast("Enable Wi-Fi first", 1100);
        return;
    }
    hidePasswordPanel();
    if (wifiMgrStartScan()) {
        updateHeaderAndControls();
        rebuildNetworkList();
    } else {
        m3ShowToast("Scan unavailable", 1100);
    }
}

static void forgetBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    hidePasswordPanel();
    wifiMgrForgetSaved();
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
    updateHeaderAndControls();
    rebuildNetworkList();
    m3ShowToast("Saved Wi-Fi forgotten", 1200);
}

static void netClickCb(lv_event_t* e) {
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const WifiScanEntry* entry = wifiMgrGetScanResult(idx);
    if (!entry) return;
    m3PulseFeedback();

    if (!g_sys.wifiEnabled || g_sys.airplaneMode) {
        m3ShowToast("Enable Wi-Fi first", 1100);
        return;
    }

    if (g_sys.wifiConnected && strcmp(entry->ssid, g_sys.wifiSsid) == 0) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Connected: %s", g_sys.wifiIp[0] ? g_sys.wifiIp : entry->ssid);
        m3ShowToast(msg, 1400);
        return;
    }

    if (entry->isOpen) {
        hidePasswordPanel();
        wifiMgrConnect(entry->ssid, "", true);
        updateHeaderAndControls();
        statusBarRefresh();
        if (quickSettingsIsOpen()) quickSettingsRefresh();
        char msg[56];
        snprintf(msg, sizeof(msg), "Connecting to %s...", entry->ssid);
        m3ShowToast(msg, 1400);
        return;
    }

    showPasswordPanel(entry->ssid);
}

static void updateHeaderAndControls() {
    if (!s_body) return;

    if (s_toggleSw) {
        bool active = g_sys.wifiEnabled && !g_sys.airplaneMode;
        if (active) lv_obj_add_state(s_toggleSw, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_toggleSw, LV_STATE_CHECKED);
    }

    if (s_toggleSub) {
        char sub[48];
        if (!g_sys.wifiEnabled || g_sys.airplaneMode) {
            snprintf(sub, sizeof(sub), "Off");
        } else if (g_sys.wifiConnected) {
            if (g_sys.wifiIp[0]) snprintf(sub, sizeof(sub), "%s", g_sys.wifiIp);
            else snprintf(sub, sizeof(sub), "Connected");
        } else if (g_sys.wifiConnecting) {
            snprintf(sub, sizeof(sub), "Connecting...");
        } else if (wifiMgrIsScanning()) {
            snprintf(sub, sizeof(sub), "Scanning 2.4 GHz...");
        } else {
            snprintf(sub, sizeof(sub), "802.11 b/g/n");
        }
        lv_label_set_text(s_toggleSub, sub);
    }

    if (s_scanBtnLbl) {
        lv_label_set_text(s_scanBtnLbl, wifiMgrIsScanning() ? "Scanning..." : LV_SYMBOL_REFRESH "  Scan");
    }

    if (s_forgetBtn) {
        if (wifiMgrHasSavedCreds()) lv_obj_clear_flag(s_forgetBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_forgetBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuildNetworkList() {
    if (!s_listBox) return;
    lv_obj_clean(s_listBox);

    if (!g_sys.wifiEnabled || g_sys.airplaneMode) {
        lv_obj_t* msg = lv_label_create(s_listBox);
        lv_label_set_text(msg, "Turn on Wi-Fi to scan 2.4 GHz networks");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_set_width(msg, LV_PCT(100));
        return;
    }

    size_t count = wifiMgrGetScanCount();
    if (count == 0) {
        lv_obj_t* msg = lv_label_create(s_listBox);
        lv_label_set_text(msg, wifiMgrIsScanning() ? "Scanning 2.4 GHz networks..." : "No 2.4 GHz networks found");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_set_width(msg, LV_PCT(100));
        return;
    }

    const char* savedSsid = wifiMgrGetSavedSsid();
    for (size_t i = 0; i < count; ++i) {
        const WifiScanEntry* e = wifiMgrGetScanResult(i);
        if (!e) continue;

        bool isConn = g_sys.wifiConnected && strcmp(e->ssid, g_sys.wifiSsid) == 0;
        bool isConnecting = g_sys.wifiConnecting && strcmp(e->ssid, g_sys.wifiSsid) == 0;
        bool isSaved = savedSsid[0] != '\0' && strcmp(e->ssid, savedSsid) == 0;

        lv_obj_t* card = m3Card(s_listBox, LV_PCT(100), 44, 12);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 6, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, netClickCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        uint32_t icoCol = isConn ? m3PrimaryHex() :
                          isSaved ? M3Color::TERTIARY : m3TextSecondaryHex();
        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(ico, lv_color_hex(icoCol), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_12, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, e->ssid);
        lv_obj_set_style_text_color(name, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_width(name, 166);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, -1);

        char subBuf[56];
        const char* auth = wifiMgrAuthModeName(e->authMode);
        if (isConn) {
            snprintf(subBuf, sizeof(subBuf), "%s  •  Connected (%d dBm)", auth, (int)e->rssi);
        } else if (isConnecting) {
            snprintf(subBuf, sizeof(subBuf), "%s  •  Connecting...", auth);
        } else if (isSaved) {
            snprintf(subBuf, sizeof(subBuf), "%s  •  Saved (%d dBm)", auth, (int)e->rssi);
        } else {
            snprintf(subBuf, sizeof(subBuf), "%s  (%d dBm)", auth, (int)e->rssi);
        }
        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, subBuf);
        lv_obj_set_style_text_color(sub, lv_color_hex(isConn ? m3PrimaryHex() : m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_set_width(sub, 174);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 22, 1);
    }
}

static void refreshAppWifiTheme() {
    if (!s_body) return;
    if (s_toggleRow) {
        lv_obj_set_style_bg_color(s_toggleRow, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_toggleRow, lv_color_hex(m3OutlineHex()), 0);
        lv_obj_t* title = lv_obj_get_child(s_toggleRow, 1);
        if (title) lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        if (s_toggleSub) lv_obj_set_style_text_color(s_toggleSub, lv_color_hex(m3TextSecondaryHex()), 0);
    }
    if (s_passCard) {
        lv_obj_set_style_bg_color(s_passCard, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_passCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_passTitle) {
        lv_obj_set_style_text_color(s_passTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    if (s_passTa) {
        lv_obj_set_style_bg_color(s_passTa, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_text_color(s_passTa, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_border_color(s_passTa, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_passCancelBtn) {
        lv_obj_set_style_bg_color(s_passCancelBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
        lv_obj_t* lbl = lv_obj_get_child(s_passCancelBtn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    rebuildNetworkList();
}

static void wifiDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    s_toggleRow = nullptr;
    s_toggleSub = nullptr;
    s_toggleSw = nullptr;
    s_scanBtn = nullptr;
    s_scanBtnLbl = nullptr;
    s_forgetBtn = nullptr;
    s_passCard = nullptr;
    s_passTitle = nullptr;
    s_passTa = nullptr;
    s_passCancelBtn = nullptr;
    s_listBox = nullptr;
    s_targetSsid[0] = '\0';
}

void buildAppWifi(lv_obj_t* body) {
    s_body = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, wifiDeleteCb, LV_EVENT_DELETE, nullptr);

    s_toggleRow = m3ToggleRow(body, LV_SYMBOL_WIFI, "Use Wi-Fi", "802.11 b/g/n",
                              g_sys.wifiEnabled && !g_sys.airplaneMode, M3Color::PRIMARY, wifiSwCb);
    // m3ToggleRow child order with icon+subtitle: 0=ico, 1=title, 2=subtitle, 3=switch
    s_toggleSub = lv_obj_get_child(s_toggleRow, 2);
    s_toggleSw  = lv_obj_get_child(s_toggleRow, 3);

    // Action row: Scan + Forget
    lv_obj_t* actRow = lv_obj_create(body);
    lv_obj_set_size(actRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(actRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actRow, 0, 0);
    lv_obj_set_style_pad_all(actRow, 0, 0);
    lv_obj_set_flex_flow(actRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actRow, 6, 0);
    lv_obj_clear_flag(actRow, LV_OBJ_FLAG_SCROLLABLE);

    s_scanBtn = m3PillBtn(actRow, LV_SYMBOL_REFRESH "  Scan", M3Color::PRIMARY_CONT, M3Color::PRIMARY, 108, 28);
    s_scanBtnLbl = lv_obj_get_child(s_scanBtn, 0);
    lv_obj_add_event_cb(s_scanBtn, scanBtnCb, LV_EVENT_CLICKED, nullptr);

    s_forgetBtn = m3PillBtn(actRow, LV_SYMBOL_TRASH "  Forget", 0x3D1E22, M3Color::CORAL, 102, 28);
    lv_obj_add_event_cb(s_forgetBtn, forgetBtnCb, LV_EVENT_CLICKED, nullptr);

    // Inline password entry card (hidden until a secured network is tapped)
    s_passCard = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_flex_flow(s_passCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_passCard, 6, 0);
    lv_obj_add_flag(s_passCard, LV_OBJ_FLAG_HIDDEN);

    s_passTitle = lv_label_create(s_passCard);
    lv_label_set_text(s_passTitle, "Password");
    lv_obj_set_style_text_font(s_passTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_passTitle, LV_PCT(100));
    lv_label_set_long_mode(s_passTitle, LV_LABEL_LONG_DOT);

    s_passTa = lv_textarea_create(s_passCard);
    lv_obj_set_size(s_passTa, LV_PCT(100), 34);
    lv_textarea_set_one_line(s_passTa, true);
    lv_textarea_set_max_length(s_passTa, WIFI_PASS_MAX_LEN);
    lv_textarea_set_placeholder_text(s_passTa, "Enter Wi-Fi password");
    lv_obj_set_style_text_font(s_passTa, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(s_passTa, 1, 0);
    lv_obj_set_style_radius(s_passTa, 10, 0);
    lv_obj_set_style_pad_hor(s_passTa, 10, 0);
    lv_obj_set_style_pad_ver(s_passTa, 7, 0);
    m3KeyboardAttach(s_passTa, onPassEnter);

    lv_obj_t* passBtns = lv_obj_create(s_passCard);
    lv_obj_set_size(passBtns, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(passBtns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passBtns, 0, 0);
    lv_obj_set_style_pad_all(passBtns, 0, 0);
    lv_obj_set_flex_flow(passBtns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(passBtns, 6, 0);
    lv_obj_clear_flag(passBtns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* connBtn = m3PillBtn(passBtns, "Connect", M3Color::PRIMARY_CONT, M3Color::PRIMARY, 96, 28);
    lv_obj_add_event_cb(connBtn, passConnectBtnCb, LV_EVENT_CLICKED, nullptr);

    s_passCancelBtn = m3PillBtn(passBtns, "Cancel", m3SurfaceHighestHex(), m3TextPrimaryHex(), 86, 28);
    lv_obj_add_event_cb(s_passCancelBtn, passCancelBtnCb, LV_EVENT_CLICKED, nullptr);

    // Network list container
    s_listBox = lv_obj_create(body);
    lv_obj_set_size(s_listBox, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_listBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_listBox, 0, 0);
    lv_obj_set_style_pad_all(s_listBox, 0, 0);
    lv_obj_set_flex_flow(s_listBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_listBox, 6, 0);
    lv_obj_clear_flag(s_listBox, LV_OBJ_FLAG_SCROLLABLE);

    // Auto-start async scan on open if Wi-Fi is enabled and no results yet
    if (g_sys.wifiEnabled && !g_sys.airplaneMode && !wifiMgrIsScanning() && wifiMgrGetScanCount() == 0) {
        wifiMgrStartScan();
    }

    s_lastScanGen = wifiMgrGetScanGen();
    s_lastScanning = wifiMgrIsScanning();
    s_lastEnabled = g_sys.wifiEnabled && !g_sys.airplaneMode;
    s_lastConnected = g_sys.wifiConnected;
    s_lastConnecting = g_sys.wifiConnecting;
    s_lastHasSaved = wifiMgrHasSavedCreds();
    strncpy(s_lastIp, g_sys.wifiIp, sizeof(s_lastIp) - 1);
    s_lastIp[sizeof(s_lastIp) - 1] = '\0';

    updateHeaderAndControls();
    refreshAppWifiTheme();
}

void tickAppWifi() {
    if (!s_body) return;

    WifiConnEvent ev = wifiMgrConsumeEvent();
    if (ev == WifiConnEvent::CONNECTED) {
        char msg[56];
        snprintf(msg, sizeof(msg), "Connected to %s", g_sys.wifiSsid);
        m3ShowToast(msg, 1300);
    } else if (ev == WifiConnEvent::FAILED) {
        m3ShowToast("Wi-Fi connection failed", 1400);
    } else if (ev == WifiConnEvent::DISCONNECTED) {
        m3ShowToast("Wi-Fi disconnected", 1200);
    }

    uint32_t gen = wifiMgrGetScanGen();
    bool scanning = wifiMgrIsScanning();
    bool enabled = g_sys.wifiEnabled && !g_sys.airplaneMode;
    bool hasSaved = wifiMgrHasSavedCreds();
    bool ipChanged = (strcmp(s_lastIp, g_sys.wifiIp) != 0);

    bool dirty = (gen != s_lastScanGen) ||
                 (scanning != s_lastScanning) ||
                 (enabled != s_lastEnabled) ||
                 (g_sys.wifiConnected != s_lastConnected) ||
                 (g_sys.wifiConnecting != s_lastConnecting) ||
                 (hasSaved != s_lastHasSaved) ||
                 ipChanged;

    if (dirty) {
        s_lastScanGen = gen;
        s_lastScanning = scanning;
        s_lastEnabled = enabled;
        s_lastConnected = g_sys.wifiConnected;
        s_lastConnecting = g_sys.wifiConnecting;
        s_lastHasSaved = hasSaved;
        strncpy(s_lastIp, g_sys.wifiIp, sizeof(s_lastIp) - 1);
        s_lastIp[sizeof(s_lastIp) - 1] = '\0';

        updateHeaderAndControls();
        rebuildNetworkList();
    }
}

REGISTER_APP_THEMED(wifi, "wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x8AB4F8, 0x1A3252, buildAppWifi, tickAppWifi, refreshAppWifiTheme, 40);
