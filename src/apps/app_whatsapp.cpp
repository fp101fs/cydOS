#include "app_registry.h"
#include "../assets/icon_whatsapp.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include "../components/m3_modal.h"
#include "../drivers/companion_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum class WaView : uint8_t {
    INBOX = 0,
    DETAIL,
    SETUP
};

static lv_obj_t* s_body       = nullptr;
static lv_obj_t* s_replyTa    = nullptr;
static lv_obj_t* s_sendBtn    = nullptr;
static lv_obj_t* s_replyStLbl = nullptr;
static lv_obj_t* s_hostTa     = nullptr;
static lv_obj_t* s_portTa     = nullptr;
static lv_obj_t* s_tokenTa    = nullptr;
static lv_obj_t* s_setupStLbl = nullptr;

static WaView             s_view        = WaView::INBOX;
static char               s_activeId[COMPANION_ID_MAX] = "";
static uint32_t           s_lastGen     = 0;
static CompanionConnState s_lastState   = CompanionConnState::UNCONFIGURED;
static uint32_t           s_handledReq  = 0;

static void renderInboxView();
static void renderDetailView();
static void renderSetupView();

static uint32_t waAccentHex() {
    return g_sys.lightMode ? 0x128C7E : 0x25D366;
}

static uint32_t waBtnFgHex() {
    return g_sys.lightMode ? 0xFFFFFF : 0x062A14;
}

static uint32_t statusColorHex(CompanionConnState st) {
    switch (st) {
        case CompanionConnState::CONNECTED:   return waAccentHex();
        case CompanionConnState::CONNECTING:  return M3Color::AMBER;
        case CompanionConnState::AUTH_FAILED: return M3Color::CORAL;
        default:                              return m3TextSecondaryHex();
    }
}

static void clearViewPtrs() {
    s_replyTa    = nullptr;
    s_sendBtn    = nullptr;
    s_replyStLbl = nullptr;
    s_hostTa     = nullptr;
    s_portTa     = nullptr;
    s_tokenTa    = nullptr;
    s_setupStLbl = nullptr;
}

static void waDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    clearViewPtrs();
}

static void styleTextarea(lv_obj_t* ta) {
    if (!ta) return;
    lv_obj_set_style_bg_color(ta, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(waAccentHex()), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_opa(ta, LV_OPA_60, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_pad_hor(ta, 8, 0);
    lv_obj_set_style_pad_ver(ta, 5, 0);
}

static void updateReplyStatusUi() {
    if (!s_replyStLbl) return;

    CompanionReplyState rs;
    companionGetReplyState(&rs);

    bool matchesActive = (rs.notifId[0] && strcmp(rs.notifId, s_activeId) == 0);
    if (!matchesActive || rs.status == CompanionReplyStatus::IDLE) {
        lv_label_set_text(s_replyStLbl, companionIsConnected() ? "Ready to reply" : "Offline - connect to reply");
        lv_obj_set_style_text_color(s_replyStLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
        return;
    }

    switch (rs.status) {
        case CompanionReplyStatus::SENDING:
            lv_label_set_text(s_replyStLbl, "Sending reply...");
            lv_obj_set_style_text_color(s_replyStLbl, lv_color_hex(M3Color::AMBER), 0);
            if (s_sendBtn) lv_obj_add_state(s_sendBtn, LV_STATE_DISABLED);
            break;

        case CompanionReplyStatus::SENT:
            lv_label_set_text(s_replyStLbl, "Reply sent");
            lv_obj_set_style_text_color(s_replyStLbl, lv_color_hex(waAccentHex()), 0);
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                if (s_replyTa) lv_textarea_set_text(s_replyTa, "");
                m3ShowToast("WhatsApp reply sent", 1300);
            }
            break;

        case CompanionReplyStatus::UNSUPPORTED: {
            char buf[80];
            snprintf(buf, sizeof(buf), "Unsupported: %s",
                     rs.detail[0] ? rs.detail : "No reply action");
            lv_label_set_text(s_replyStLbl, buf);
            lv_obj_set_style_text_color(s_replyStLbl, lv_color_hex(M3Color::AMBER), 0);
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                m3ShowToast("Reply unavailable", 1500);
            }
            break;
        }

        case CompanionReplyStatus::FAILED:
        default: {
            char buf[80];
            snprintf(buf, sizeof(buf), "Failed: %s",
                     rs.detail[0] ? rs.detail : "Could not send");
            lv_label_set_text(s_replyStLbl, buf);
            lv_obj_set_style_text_color(s_replyStLbl, lv_color_hex(M3Color::CORAL), 0);
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                m3ShowToast("Reply failed (text kept)", 1600);
            }
            break;
        }
    }
}

// ============================================================================
// Callbacks
// ============================================================================

static void openHelpModalCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();

    static const M3ModalStep kSteps[] = {
        { "What Is This?",
          "Read and reply to WhatsApp on CYD.\n\nUses your Android phone on local Wi-Fi." },
        { "1. Same Wi-Fi",
          "Connect phone and CYD to same Wi-Fi.\n\nNo cloud or login is needed." },
        { "2. Open Phone App",
          "Open Pixel CYD Companion on Android.\n\nPhone IP and 6-digit token appear right away." },
        { "3. Allow Access",
          "Tap Grant Notification Access on your phone.\n\nSwitch on Pixel CYD WhatsApp Bridge." },
        { "4. Connect CYD",
          "Tap the green gear icon here.\n\nEnter Phone IP and token, then tap Connect." },
    };
    m3ModalShowSteps(kSteps, sizeof(kSteps) / sizeof(kSteps[0]), waAccentHex());
}

static void openSetupCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    s_view = WaView::SETUP;
    renderSetupView();
}

static void syncBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (!companionIsConfigured()) {
        s_view = WaView::SETUP;
        renderSetupView();
        return;
    }
    if (companionIsConnected()) {
        companionRequestHistory();
        m3ShowToast("Syncing messages...", 1100);
    } else {
        companionConnectNow();
        m3ShowToast("Reconnecting...", 1100);
    }
}

static void openMsgCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    CompanionMessage msg;
    if (!companionGetMessage(idx, &msg)) return;

    strncpy(s_activeId, msg.id, sizeof(s_activeId) - 1);
    s_activeId[sizeof(s_activeId) - 1] = '\0';
    companionMarkRead(msg.id);
    companionResetReplyState();
    s_handledReq = 0;

    s_view = WaView::DETAIL;
    renderDetailView();
}

static void backToInboxCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    s_view = WaView::INBOX;
    renderInboxView();
}

static void triggerSendReply() {
    if (!s_replyTa || !s_activeId[0]) return;
    const char* txt = lv_textarea_get_text(s_replyTa);
    if (!txt || !txt[0]) {
        m3ShowToast("Type a reply first", 1100);
        return;
    }
    if (!companionIsConnected()) {
        m3ShowToast("Phone disconnected", 1300);
        return;
    }

    uint32_t reqId = companionSendReply(s_activeId, txt);
    if (reqId == 0) {
        m3ShowToast("Already sending...", 1000);
        return;
    }
    updateReplyStatusUi();
}

static void sendBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    triggerSendReply();
}

static void onReplyEnter(const char* txt) {
    (void)txt;
    triggerSendReply();
}

static void dismissBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    if (!s_activeId[0]) return;

    if (companionIsConnected() && companionDismissNotification(s_activeId)) {
        m3ShowToast("Dismissed on phone", 1200);
    } else {
        m3ShowToast("Offline: cannot dismiss", 1300);
        return;
    }
    s_view = WaView::INBOX;
    renderInboxView();
}

static void saveConnectCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    if (!s_hostTa || !s_portTa || !s_tokenTa) return;

    const char* host  = lv_textarea_get_text(s_hostTa);
    const char* portS = lv_textarea_get_text(s_portTa);
    const char* token = lv_textarea_get_text(s_tokenTa);

    if (!host || !host[0]) {
        m3ShowToast("Enter phone IP first", 1300);
        return;
    }

    long p = strtol(portS ? portS : "8765", nullptr, 10);
    if (p <= 0 || p > 65535) p = COMPANION_DEFAULT_PORT;

    companionSetConfig(host, (uint16_t)p, token ? token : "");
    m3ShowToast("Saved & connecting...", 1200);
    s_view = WaView::INBOX;
    renderInboxView();
}

static void disconnectCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    companionDisconnect();
    m3ShowToast("Disconnected", 1100);
    if (s_view == WaView::SETUP) renderSetupView();
}

static void onSetupEnter(const char* txt) {
    (void)txt;
    if (m3KeyboardIsOpen()) m3KeyboardHide();
}

// ============================================================================
// Views
// ============================================================================

static void renderInboxView() {
    if (!s_body) return;
    clearViewPtrs();
    lv_obj_clean(s_body);

    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    // Top bar: Connection status + Sync + Setup
    lv_obj_t* topRow = lv_obj_create(s_body);
    lv_obj_set_size(topRow, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topRow, 4, 0);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    CompanionConnState st = companionGetState();
    uint8_t unread = companionUnreadCount();

    lv_obj_t* stChip = m3Card(topRow, 0, 26, 13);
    lv_obj_set_flex_grow(stChip, 1);
    lv_obj_set_style_pad_hor(stChip, 8, 0);
    lv_obj_set_style_pad_ver(stChip, 0, 0);
    lv_obj_set_flex_flow(stChip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stChip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(stChip, 5, 0);

    lv_obj_t* dot = lv_obj_create(stChip);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(statusColorHex(st)), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    char stBuf[36];
    if (unread > 0) {
        snprintf(stBuf, sizeof(stBuf), "%s (%u)", companionStatusText(), (unsigned)unread);
    } else {
        snprintf(stBuf, sizeof(stBuf), "%s", companionStatusText());
    }
    lv_obj_t* stLbl = lv_label_create(stChip);
    lv_label_set_text(stLbl, stBuf);
    lv_label_set_long_mode(stLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stLbl, lv_color_hex(m3TextPrimaryHex()), 0);

    lv_obj_t* syncBtn = m3PillBtn(topRow, LV_SYMBOL_REFRESH, m3SurfaceHighestHex(), m3TextPrimaryHex(), 32, 26);
    lv_obj_add_event_cb(syncBtn, syncBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cfgBtn = m3PillBtn(topRow, LV_SYMBOL_SETTINGS, waAccentHex(), waBtnFgHex(), 34, 26);
    lv_obj_add_event_cb(cfgBtn, openSetupCb, LV_EVENT_CLICKED, nullptr);

    // Message list container
    lv_obj_t* listCont = lv_obj_create(s_body);
    lv_obj_set_width(listCont, LV_PCT(100));
    lv_obj_set_flex_grow(listCont, 1);
    lv_obj_set_style_bg_opa(listCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listCont, 0, 0);
    lv_obj_set_style_pad_all(listCont, 0, 0);
    lv_obj_set_flex_flow(listCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listCont, 6, 0);
    lv_obj_set_scroll_dir(listCont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(listCont, LV_SCROLLBAR_MODE_AUTO);

    size_t count = companionGetMessageCount();
    if (count == 0) {
        lv_obj_t* emptyCard = m3Card(listCont, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_style_pad_all(emptyCard, 12, 0);
        lv_obj_set_flex_flow(emptyCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(emptyCard, 6, 0);

        lv_obj_t* title = lv_label_create(emptyCard);
        lv_obj_set_width(title, LV_PCT(100));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);

        lv_obj_t* sub = lv_label_create(emptyCard);
        lv_obj_set_width(sub, LV_PCT(100));
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);

        if (!g_sys.wifiConnected) {
            lv_label_set_text(title, "Wi-Fi not connected");
            lv_label_set_text(sub, "Connect to the same Wi-Fi as your Android phone first.");
        } else if (st == CompanionConnState::UNCONFIGURED) {
            lv_label_set_text(title, "Link Android Companion");
            lv_label_set_text(sub, "Tap the green gear icon above and enter your phone's IP and 6-digit pairing token.");
        } else if (st == CompanionConnState::AUTH_FAILED) {
            lv_label_set_text(title, "Pairing token mismatch");
            lv_label_set_text(sub, "Check the 6-digit token in the Android Companion app and update Setup.");
        } else if (st != CompanionConnState::CONNECTED) {
            char err[COMPANION_DETAIL_MAX];
            companionGetLastError(err, sizeof(err));
            lv_label_set_text(title, "Phone offline");
            lv_label_set_text(sub, err[0] ? err : "Open Pixel CYD Companion on your Android phone and tap refresh.");
        } else {
            lv_label_set_text(title, "No WhatsApp messages");
            lv_label_set_text(sub, "New WhatsApp notifications from your phone will appear here.");
        }

        // Big "?" help button just below the status / Link Android Companion card
        lv_obj_t* helpWrap = lv_obj_create(listCont);
        lv_obj_set_size(helpWrap, LV_PCT(100), 66);
        lv_obj_set_style_bg_opa(helpWrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(helpWrap, 0, 0);
        lv_obj_set_style_pad_all(helpWrap, 0, 0);
        lv_obj_clear_flag(helpWrap, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* helpBtn = lv_btn_create(helpWrap);
        lv_obj_set_size(helpBtn, 54, 54);
        lv_obj_center(helpBtn);
        lv_obj_set_style_radius(helpBtn, M3Dim::RADIUS_PILL, 0);
        lv_obj_set_style_bg_color(helpBtn, lv_color_hex(waAccentHex()), 0);
        lv_obj_set_style_bg_opa(helpBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(helpBtn, 8, 0);
        lv_obj_set_style_shadow_opa(helpBtn, LV_OPA_30, 0);
        lv_obj_set_style_shadow_ofs_y(helpBtn, 2, 0);
        lv_obj_add_event_cb(helpBtn, openHelpModalCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* helpLbl = lv_label_create(helpBtn);
        lv_label_set_text(helpLbl, "?");
        lv_obj_set_style_text_font(helpLbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(helpLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(helpLbl);
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        CompanionMessage msg;
        if (!companionGetMessage(i, &msg)) continue;

        lv_obj_t* card = m3Card(listCont, LV_PCT(100), 48, 12);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 6, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, openMsgCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        lv_coord_t textLeft = msg.unread ? 11 : 0;
        if (msg.unread) {
            lv_obj_t* udot = lv_obj_create(card);
            lv_obj_set_size(udot, 6, 6);
            lv_obj_set_style_radius(udot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(udot, lv_color_hex(waAccentHex()), 0);
            lv_obj_set_style_bg_opa(udot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(udot, 0, 0);
            lv_obj_align(udot, LV_ALIGN_TOP_LEFT, 0, 4);
        }

        lv_obj_t* senderLbl = lv_label_create(card);
        lv_label_set_text(senderLbl, msg.sender);
        lv_label_set_long_mode(senderLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(senderLbl, 146 - textLeft);
        lv_obj_set_style_text_font(senderLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(senderLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_align(senderLbl, LV_ALIGN_TOP_LEFT, textLeft, 0);

        lv_obj_t* timeLbl = lv_label_create(card);
        lv_label_set_text(timeLbl, msg.timeStr[0] ? msg.timeStr : "");
        lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(timeLbl, lv_color_hex(msg.unread ? waAccentHex() : m3TextSecondaryHex()), 0);
        lv_obj_align(timeLbl, LV_ALIGN_TOP_RIGHT, 0, 1);

        lv_obj_t* prevLbl = lv_label_create(card);
        lv_label_set_text(prevLbl, msg.text[0] ? msg.text : "(no preview)");
        lv_label_set_long_mode(prevLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(prevLbl, 196);
        lv_obj_set_style_text_font(prevLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(prevLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_align(prevLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

static void renderDetailView() {
    if (!s_body) return;
    clearViewPtrs();
    lv_obj_clean(s_body);

    CompanionMessage msg;
    if (!companionFindMessageById(s_activeId, &msg)) {
        s_view = WaView::INBOX;
        renderInboxView();
        return;
    }

    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    // Header row: Back + Sender + Time
    lv_obj_t* hdr = lv_obj_create(s_body);
    lv_obj_set_size(hdr, LV_PCT(100), 28);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = m3PillBtn(hdr, LV_SYMBOL_LEFT, m3SurfaceHighestHex(), m3TextPrimaryHex(), 34, 26);
    lv_obj_add_event_cb(backBtn, backToInboxCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* nameLbl = lv_label_create(hdr);
    lv_label_set_text(nameLbl, msg.sender);
    lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(nameLbl, 1);
    lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);

    if (msg.timeStr[0]) {
        lv_obj_t* tLbl = lv_label_create(hdr);
        lv_label_set_text(tLbl, msg.timeStr);
        lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(tLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    }

    // Message preview card (scrollable for longer previews)
    lv_obj_t* msgCard = m3Card(s_body, LV_PCT(100), 68, 12);
    lv_obj_set_flex_grow(msgCard, 1);
    lv_obj_set_style_pad_all(msgCard, 10, 0);
    lv_obj_add_flag(msgCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(msgCard, LV_DIR_VER);

    lv_obj_t* bodyLbl = lv_label_create(msgCard);
    lv_label_set_text(bodyLbl, msg.text[0] ? msg.text : "(No message preview)");
    lv_label_set_long_mode(bodyLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bodyLbl, LV_PCT(100));
    lv_obj_set_style_text_font(bodyLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bodyLbl, lv_color_hex(m3TextPrimaryHex()), 0);

    if (msg.canReply) {
        s_replyTa = lv_textarea_create(s_body);
        lv_obj_set_size(s_replyTa, LV_PCT(100), 34);
        lv_textarea_set_one_line(s_replyTa, true);
        lv_textarea_set_max_length(s_replyTa, 180);
        lv_textarea_set_placeholder_text(s_replyTa, "Type WhatsApp reply...");
        styleTextarea(s_replyTa);
        m3KeyboardAttach(s_replyTa, onReplyEnter);

        s_replyStLbl = lv_label_create(s_body);
        lv_obj_set_width(s_replyStLbl, LV_PCT(100));
        lv_label_set_long_mode(s_replyStLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_replyStLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* bar = lv_obj_create(s_body);
        lv_obj_set_size(bar, LV_PCT(100), 30);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(bar, 6, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        s_sendBtn = m3PillBtn(bar, LV_SYMBOL_OK " Send", waAccentHex(), waBtnFgHex(), 0, 30);
        lv_obj_set_flex_grow(s_sendBtn, 1);
        lv_obj_add_event_cb(s_sendBtn, sendBtnCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* disBtn = m3PillBtn(bar, LV_SYMBOL_CLOSE " Dismiss", m3SurfaceHighestHex(), m3TextPrimaryHex(), 84, 30);
        lv_obj_add_event_cb(disBtn, dismissBtnCb, LV_EVENT_CLICKED, nullptr);

        updateReplyStatusUi();
    } else {
        lv_obj_t* noteLbl = lv_label_create(s_body);
        lv_label_set_text(noteLbl, "Reply unavailable for this notification");
        lv_label_set_long_mode(noteLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(noteLbl, LV_PCT(100));
        lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(noteLbl, lv_color_hex(M3Color::AMBER), 0);

        lv_obj_t* disBtn = m3PillBtn(s_body, LV_SYMBOL_CLOSE " Dismiss on Phone", m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 30);
        lv_obj_add_event_cb(disBtn, dismissBtnCb, LV_EVENT_CLICKED, nullptr);
    }
}

static void renderSetupView() {
    if (!s_body) return;
    clearViewPtrs();
    lv_obj_clean(s_body);

    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 5, 0);

    char host[COMPANION_HOST_MAX];
    char token[COMPANION_TOKEN_MAX];
    char portStr[8];
    companionGetHost(host, sizeof(host));
    companionGetToken(token, sizeof(token));
    snprintf(portStr, sizeof(portStr), "%u", (unsigned)companionGetPort());

    lv_obj_t* ipLbl = lv_label_create(s_body);
    lv_label_set_text(ipLbl, "Android Phone IP (local Wi-Fi)");
    lv_obj_set_style_text_font(ipLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ipLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    s_hostTa = lv_textarea_create(s_body);
    lv_obj_set_size(s_hostTa, LV_PCT(100), 30);
    lv_textarea_set_one_line(s_hostTa, true);
    lv_textarea_set_max_length(s_hostTa, COMPANION_HOST_MAX - 1);
    lv_textarea_set_placeholder_text(s_hostTa, "192.168.1.50");
    lv_textarea_set_text(s_hostTa, host);
    styleTextarea(s_hostTa);
    m3KeyboardAttach(s_hostTa, onSetupEnter);

    // Port & Token row
    lv_obj_t* lblRow = lv_obj_create(s_body);
    lv_obj_set_size(lblRow, LV_PCT(100), 14);
    lv_obj_set_style_bg_opa(lblRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lblRow, 0, 0);
    lv_obj_set_style_pad_all(lblRow, 0, 0);
    lv_obj_set_flex_flow(lblRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lblRow, 6, 0);
    lv_obj_clear_flag(lblRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* pLbl = lv_label_create(lblRow);
    lv_label_set_text(pLbl, "Port");
    lv_obj_set_width(pLbl, 72);
    lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(pLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    lv_obj_t* tLbl = lv_label_create(lblRow);
    lv_label_set_text(tLbl, "Pairing Token");
    lv_obj_set_flex_grow(tLbl, 1);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    lv_obj_t* inpRow = lv_obj_create(s_body);
    lv_obj_set_size(inpRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(inpRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inpRow, 0, 0);
    lv_obj_set_style_pad_all(inpRow, 0, 0);
    lv_obj_set_flex_flow(inpRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(inpRow, 6, 0);
    lv_obj_clear_flag(inpRow, LV_OBJ_FLAG_SCROLLABLE);

    s_portTa = lv_textarea_create(inpRow);
    lv_obj_set_size(s_portTa, 72, 30);
    lv_textarea_set_one_line(s_portTa, true);
    lv_textarea_set_max_length(s_portTa, 5);
    lv_textarea_set_text(s_portTa, portStr);
    styleTextarea(s_portTa);
    m3KeyboardAttach(s_portTa, onSetupEnter);

    s_tokenTa = lv_textarea_create(inpRow);
    lv_obj_set_height(s_tokenTa, 30);
    lv_obj_set_flex_grow(s_tokenTa, 1);
    lv_textarea_set_one_line(s_tokenTa, true);
    lv_textarea_set_max_length(s_tokenTa, COMPANION_TOKEN_MAX - 1);
    lv_textarea_set_placeholder_text(s_tokenTa, "6-digit code");
    lv_textarea_set_text(s_tokenTa, token);
    styleTextarea(s_tokenTa);
    m3KeyboardAttach(s_tokenTa, onSetupEnter);

    // Status info
    char info[96];
    char err[COMPANION_DETAIL_MAX];
    companionGetLastError(err, sizeof(err));
    snprintf(info, sizeof(info), "CYD IP: %s | %s%s%s",
             g_sys.wifiConnected ? g_sys.wifiIp : "No Wi-Fi",
             companionStatusText(),
             err[0] ? " - " : "",
             err[0] ? err : "");
    s_setupStLbl = lv_label_create(s_body);
    lv_label_set_text(s_setupStLbl, info);
    lv_label_set_long_mode(s_setupStLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_setupStLbl, LV_PCT(100));
    lv_obj_set_flex_grow(s_setupStLbl, 1);
    lv_obj_set_style_text_font(s_setupStLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_setupStLbl, lv_color_hex(m3TextSecondaryHex()), 0);

    // Buttons: Back | Connect | Disc.
    lv_obj_t* bar = lv_obj_create(s_body);
    lv_obj_set_size(bar, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 5, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = m3PillBtn(bar, LV_SYMBOL_LEFT, m3SurfaceHighestHex(), m3TextPrimaryHex(), 36, 30);
    lv_obj_add_event_cb(backBtn, backToInboxCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* connBtn = m3PillBtn(bar, LV_SYMBOL_OK " Connect", waAccentHex(), waBtnFgHex(), 0, 30);
    lv_obj_set_flex_grow(connBtn, 1);
    lv_obj_add_event_cb(connBtn, saveConnectCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* discBtn = m3PillBtn(bar, "Off", m3SurfaceHighestHex(), m3TextPrimaryHex(), 44, 30);
    lv_obj_add_event_cb(discBtn, disconnectCb, LV_EVENT_CLICKED, nullptr);
}

// ============================================================================
// App Entry, Tick & Theme Refresh
// ============================================================================

static void tickAppWhatsapp() {
    if (!s_body) return;
    uint32_t gen = companionGetDataGen();
    CompanionConnState st = companionGetState();

    if (gen == s_lastGen && st == s_lastState) return;
    s_lastGen = gen;
    s_lastState = st;

    if (s_view == WaView::INBOX) {
        renderInboxView();
    } else if (s_view == WaView::DETAIL) {
        updateReplyStatusUi();
    } else if (s_view == WaView::SETUP && s_setupStLbl) {
        char info[96];
        char err[COMPANION_DETAIL_MAX];
        companionGetLastError(err, sizeof(err));
        snprintf(info, sizeof(info), "CYD IP: %s | %s%s%s",
                 g_sys.wifiConnected ? g_sys.wifiIp : "No Wi-Fi",
                 companionStatusText(),
                 err[0] ? " - " : "",
                 err[0] ? err : "");
        lv_label_set_text(s_setupStLbl, info);
    }
}

static void refreshAppWhatsappTheme() {
    if (!s_body) return;
    if (s_view == WaView::INBOX) {
        renderInboxView();
    } else if (s_view == WaView::DETAIL) {
        // Re-skin textarea in place to preserve unsaved reply text
        styleTextarea(s_replyTa);
    } else if (s_view == WaView::SETUP) {
        styleTextarea(s_hostTa);
        styleTextarea(s_portTa);
        styleTextarea(s_tokenTa);
    }
}

static void buildAppWhatsapp(lv_obj_t* body) {
    s_body = body;
    s_view = WaView::INBOX;
    s_activeId[0] = '\0';
    s_lastGen = companionGetDataGen();
    s_lastState = companionGetState();
    s_handledReq = 0;
    clearViewPtrs();
    lv_obj_add_event_cb(body, waDeleteCb, LV_EVENT_DELETE, nullptr);
    renderInboxView();
}

REGISTER_APP_THEMED_IMG(whatsapp, "whatsapp", "WhatsApp", LV_SYMBOL_ENVELOPE, &s_iconWhatsappImg,
                        0x25D366, 0x103923, buildAppWhatsapp, tickAppWhatsapp, refreshAppWhatsappTheme, 60);
