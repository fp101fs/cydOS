#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../ui/app_manager.h"
#include "../drivers/companion_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

static constexpr const char* COMPANION_APK_URL =
    "https://github.com/fp101fs/pixel-cyd/releases/latest/download/pixel-cyd-companion.apk";
static constexpr uint16_t PAIR_HTTP_PORT = 8766;
static constexpr int16_t  QR_CODE_SZ     = 112;
static constexpr int16_t  QR_FRAME_SZ    = 124;

enum class LinkStep : uint8_t {
    LINKED = 0,
    APK_QR,
    PAIR_QR
};

static lv_obj_t*          s_body          = nullptr;
static LinkStep           s_step          = LinkStep::APK_QR;
static CompanionConnState s_lastConnState = CompanionConnState::UNCONFIGURED;

static WiFiServer*        s_pairServer    = nullptr;
static char               s_pairKey[8]    = "";

static void renderCurrentView();

static uint32_t linkAccentHex() {
    return g_sys.lightMode ? 0x0F9D58 : 0x25D366;
}

static void stopPairServer() {
    if (s_pairServer) {
        s_pairServer->stop();
        delete s_pairServer;
        s_pairServer = nullptr;
    }
    s_pairKey[0] = '\0';
}

static void startPairServer() {
    stopPairServer();
    if (!g_sys.wifiConnected) return;

    uint32_t pin = (esp_random() % 900000U) + 100000U;
    snprintf(s_pairKey, sizeof(s_pairKey), "%06lu", (unsigned long)pin);

    s_pairServer = new WiFiServer(PAIR_HTTP_PORT);
    s_pairServer->begin();
}

static void pollPairServer() {
    if (!s_pairServer) return;
    WiFiClient client = s_pairServer->available();
    if (!client) return;

    uint32_t deadline = millis() + 450;
    String req;
    req.reserve(512);
    int contentLen = -1;
    int headerEnd = -1;

    while (client.connected() && millis() < deadline) {
        while (client.available()) {
            char ch = (char)client.read();
            if (req.length() < 1024) req += ch;
            if (headerEnd < 0) {
                headerEnd = req.indexOf("\r\n\r\n");
                if (headerEnd >= 0) {
                    int clIdx = req.indexOf("Content-Length:");
                    if (clIdx < 0) clIdx = req.indexOf("content-length:");
                    if (clIdx >= 0 && clIdx < headerEnd) {
                        contentLen = req.substring(clIdx + 15).toInt();
                    }
                }
            }
            if (headerEnd >= 0 && contentLen >= 0) {
                int bodyLen = (int)req.length() - (headerEnd + 4);
                if (bodyLen >= contentLen) break;
            }
        }
        if (headerEnd >= 0 && contentLen >= 0 && ((int)req.length() - (headerEnd + 4)) >= contentLen) {
            break;
        }
        delay(2);
    }

    bool ok = false;
    if (headerEnd >= 0) {
        String body = req.substring(headerEnd + 4);
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            const char* key   = doc["key"]   | "";
            const char* host  = doc["host"]  | "";
            const char* token = doc["token"] | "";
            int port          = doc["port"]  | (int)COMPANION_DEFAULT_PORT;
            if (port <= 0 || port > 65535) port = COMPANION_DEFAULT_PORT;

            if (s_pairKey[0] && strcmp(key, s_pairKey) == 0 && host[0] && token[0]) {
                companionSetConfig(host, (uint16_t)port, token);
                ok = true;
            }
        }
    }

    if (ok) {
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":true}");
    } else {
        client.print("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"ok\":false}");
    }
    client.flush();
    client.stop();

    if (ok) {
        stopPairServer();
        m3ShowToast("Paired! Connecting...", 1500);
        s_step = LinkStep::LINKED;
        renderCurrentView();
    }
}

static void linkDeleteCb(lv_event_t* e) {
    (void)e;
    stopPairServer();
    s_body = nullptr;
}

static void goPairStepCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_step = LinkStep::PAIR_QR;
    renderCurrentView();
}

static void goApkStepCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    stopPairServer();
    s_step = LinkStep::APK_QR;
    renderCurrentView();
}

static void openWifiCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunchById("wifi");
}

static void openWhatsappCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunchById("whatsapp");
}

static void disconnectLinkCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    companionDisconnect();
    m3ShowToast("Disconnected", 1200);
    s_step = LinkStep::APK_QR;
    renderCurrentView();
}

static lv_obj_t* createQrBox(lv_obj_t* parent, const char* payload) {
    lv_obj_t* frame = lv_obj_create(parent);
    lv_obj_set_size(frame, QR_FRAME_SZ, QR_FRAME_SZ);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 12, 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_shadow_width(frame, 10, 0);
    lv_obj_set_style_shadow_opa(frame, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(frame, 2, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* qr = lv_qrcode_create(frame, QR_CODE_SZ, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, payload, strlen(payload));
    lv_obj_center(qr);
    return frame;
}

static void renderApkQrView() {
    stopPairServer();

    lv_obj_t* hdr = lv_label_create(s_body);
    lv_label_set_text(hdr, "1. Scan to Download App");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(m3TextPrimaryHex()), 0);

    createQrBox(s_body, COMPANION_APK_URL);

    lv_obj_t* nextBtn = m3PillBtn(s_body, "App Installed  " LV_SYMBOL_RIGHT, linkAccentHex(), 0xFFFFFF, LV_PCT(100), 30);
    lv_obj_add_event_cb(nextBtn, goPairStepCb, LV_EVENT_CLICKED, nullptr);
}

static void renderPairQrView() {
    if (!g_sys.wifiConnected || !g_sys.wifiIp[0]) {
        stopPairServer();

        lv_obj_t* card = m3Card(s_body, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 6, 0);

        lv_obj_t* t = lv_label_create(card);
        lv_label_set_text(t, "Connect Wi-Fi First");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(m3TextPrimaryHex()), 0);

        lv_obj_t* s = lv_label_create(card);
        lv_label_set_text(s, "Join the same Wi-Fi as your phone to show the pairing QR.");
        lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s, LV_PCT(100));
        lv_obj_set_style_text_font(s, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(m3TextSecondaryHex()), 0);

        lv_obj_t* wifiBtn = m3PillBtn(s_body, LV_SYMBOL_WIFI " Open Wi-Fi", m3PrimaryHex(), 0xFFFFFF, LV_PCT(100), 30);
        lv_obj_add_event_cb(wifiBtn, openWifiCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* backBtn = m3PillBtn(s_body, LV_SYMBOL_LEFT " Back to APK QR", m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 28);
        lv_obj_add_event_cb(backBtn, goApkStepCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    if (!s_pairServer) {
        startPairServer();
    }

    char payload[96];
    snprintf(payload, sizeof(payload), "pixelcyd://pair?ip=%s&port=%u&key=%s",
             g_sys.wifiIp, (unsigned)PAIR_HTTP_PORT, s_pairKey);

    lv_obj_t* hdr = lv_label_create(s_body);
    lv_label_set_text(hdr, "2. Scan in Companion App");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(m3TextPrimaryHex()), 0);

    createQrBox(s_body, payload);

    lv_obj_t* bar = lv_obj_create(s_body);
    lv_obj_set_size(bar, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = m3PillBtn(bar, LV_SYMBOL_LEFT " Get APK", m3SurfaceHighestHex(), m3TextPrimaryHex(), 86, 28);
    lv_obj_add_event_cb(backBtn, goApkStepCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* stLbl = lv_label_create(bar);
    lv_label_set_text(stLbl, "Waiting for scan...");
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stLbl, lv_color_hex(linkAccentHex()), 0);
}

static void renderLinkedView() {
    stopPairServer();

    char host[COMPANION_HOST_MAX];
    companionGetHost(host, sizeof(host));
    bool connected = companionIsConnected();

    lv_obj_t* card = m3Card(s_body, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);

    lv_obj_t* titleRow = lv_obj_create(card);
    lv_obj_set_size(titleRow, LV_PCT(100), 20);
    lv_obj_set_style_bg_opa(titleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(titleRow, 0, 0);
    lv_obj_set_style_pad_all(titleRow, 0, 0);
    lv_obj_set_flex_flow(titleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(titleRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(titleRow, 6, 0);
    lv_obj_clear_flag(titleRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dot = lv_obj_create(titleRow);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(connected ? linkAccentHex() : M3Color::AMBER), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    lv_obj_t* t = lv_label_create(titleRow);
    lv_label_set_text(t, connected ? "Android Linked" : companionStatusText());
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(m3TextPrimaryHex()), 0);

    char subBuf[64];
    snprintf(subBuf, sizeof(subBuf), "Phone: %s:%u", host[0] ? host : "--", (unsigned)companionGetPort());
    lv_obj_t* s = lv_label_create(card);
    lv_label_set_text(s, subBuf);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(m3TextSecondaryHex()), 0);

    lv_obj_t* waBtn = m3PillBtn(s_body, "Open WhatsApp", linkAccentHex(), 0xFFFFFF, LV_PCT(100), 32);
    lv_obj_add_event_cb(waBtn, openWhatsappCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* row = lv_obj_create(s_body);
    lv_obj_set_size(row, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* qrBtn = m3PillBtn(row, "Pair QR", m3SurfaceHighestHex(), m3TextPrimaryHex(), 0, 28);
    lv_obj_set_flex_grow(qrBtn, 1);
    lv_obj_add_event_cb(qrBtn, goPairStepCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* discBtn = m3PillBtn(row, "Unlink", m3SurfaceHighestHex(), M3Color::CORAL, 0, 28);
    lv_obj_set_flex_grow(discBtn, 1);
    lv_obj_add_event_cb(discBtn, disconnectLinkCb, LV_EVENT_CLICKED, nullptr);
}

static void renderCurrentView() {
    if (!s_body) return;
    lv_obj_clean(s_body);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    if (s_step == LinkStep::LINKED) {
        renderLinkedView();
    } else if (s_step == LinkStep::PAIR_QR) {
        renderPairQrView();
    } else {
        renderApkQrView();
    }
}

static void buildAppAndroidLink(lv_obj_t* body) {
    s_body = body;
    s_lastConnState = companionGetState();
    s_step = companionIsConnected() ? LinkStep::LINKED : LinkStep::APK_QR;
    lv_obj_add_event_cb(body, linkDeleteCb, LV_EVENT_DELETE, nullptr);
    renderCurrentView();
}

static void tickAppAndroidLink() {
    if (!s_body) return;
    if (s_step == LinkStep::PAIR_QR) {
        pollPairServer();
    }

    CompanionConnState st = companionGetState();
    if (st != s_lastConnState) {
        s_lastConnState = st;
        if (st == CompanionConnState::CONNECTED && s_step != LinkStep::LINKED) {
            s_step = LinkStep::LINKED;
            renderCurrentView();
        } else if (s_step == LinkStep::LINKED) {
            renderCurrentView();
        }
    }
}

static void refreshAppAndroidLinkTheme() {
    if (!s_body) return;
    renderCurrentView();
}

REGISTER_APP_THEMED(android_link, "android_link", "Android Link", LV_SYMBOL_USB, 0x34D399, 0x059669,
                    buildAppAndroidLink, tickAppAndroidLink, refreshAppAndroidLinkTheme, 65);
