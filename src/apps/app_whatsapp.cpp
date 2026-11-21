#include "app_registry.h"
#include "../assets/icon_whatsapp.h"
#include "../theme/m3_theme.h"
#include "../ui/app_manager.h"
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
static lv_obj_t* s_helpBtn    = nullptr;
static lv_obj_t* s_chatScroll = nullptr;
static lv_obj_t* s_replyTa    = nullptr;
static lv_obj_t* s_sendBtn    = nullptr;
static lv_obj_t* s_hostTa     = nullptr;
static lv_obj_t* s_portTa     = nullptr;
static lv_obj_t* s_tokenTa    = nullptr;
static lv_obj_t* s_setupStLbl = nullptr;

static WaView             s_view        = WaView::INBOX;
static char               s_activeId[COMPANION_ID_MAX] = "";
static uint32_t           s_lastGen     = 0;
static CompanionConnState s_lastState   = CompanionConnState::UNCONFIGURED;
static uint32_t           s_handledReq  = 0;
static size_t             s_renderedBubbles = 0;

static void renderInboxView();
static void renderDetailView();
static void renderSetupView();
static void populateChatBubbles(const CompanionMessage& chat);

// ============================================================================
// WhatsApp Android Palette Helpers (Light & Dark)
// ============================================================================

static uint32_t waAccentHex() {
    return g_sys.lightMode ? 0x128C7E : 0x25D366;
}

static uint32_t waBtnFgHex() {
    return g_sys.lightMode ? 0xFFFFFF : 0x062A14;
}

static uint32_t waChatHeaderBgHex() {
    return g_sys.lightMode ? 0x075E54 : 0x1F2C34;
}

static uint32_t waChatHeaderFgHex() {
    return 0xFFFFFF;
}

static uint32_t waChatWallpaperHex() {
    return g_sys.lightMode ? 0xECE5DD : 0x0B141A;
}

static uint32_t waBubbleInBgHex() {
    return g_sys.lightMode ? 0xFFFFFF : 0x202C33;
}

static uint32_t waBubbleOutBgHex() {
    return g_sys.lightMode ? 0xD9FDD3 : 0x005C4B;
}

static uint32_t waBubbleTextHex() {
    return g_sys.lightMode ? 0x111B21 : 0xE9EDEF;
}

static uint32_t waBubbleTimeHex() {
    return g_sys.lightMode ? 0x667781 : 0x8696A0;
}

static uint32_t waComposerBgHex() {
    return g_sys.lightMode ? 0xFFFFFF : 0x2A3942;
}

static uint32_t waDividerHex() {
    return g_sys.lightMode ? 0xE2E6EA : 0x232F36;
}

static uint32_t waHelpBtnBgHex() {
    // Light neutral pill so the black "?" is crisp and readable in both light and dark modes
    return g_sys.lightMode ? m3SurfaceHighHex() : 0xD7DFEC;
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
    s_chatScroll = nullptr;
    s_replyTa    = nullptr;
    s_sendBtn    = nullptr;
    s_hostTa     = nullptr;
    s_portTa     = nullptr;
    s_tokenTa    = nullptr;
    s_setupStLbl = nullptr;
    s_renderedBubbles = 0;
}

static void waDeleteCb(lv_event_t* e) {
    (void)e;
    s_body    = nullptr;
    s_helpBtn = nullptr;
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

static void styleComposerTextarea(lv_obj_t* ta) {
    if (!ta) return;
    lv_obj_set_style_bg_color(ta, lv_color_hex(waComposerBgHex()), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(waBubbleTextHex()), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_radius(ta, 15, 0);
    lv_obj_set_style_pad_hor(ta, 10, 0);
    lv_obj_set_style_pad_ver(ta, 6, 0);
}

// Creates a circular WhatsApp avatar: uses actual user profile image if available,
// otherwise a circle container with the green WhatsApp icon.
// Avoids clip_corner and lv_img_set_zoom to keep LVGL heap usage minimal on ESP32.
static lv_obj_t* createWaAvatar(lv_obj_t* parent, const char* chatId, lv_coord_t sizePx) {
    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, sizePx, sizePx);
    lv_obj_set_style_radius(wrap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    const lv_img_dsc_t* photo = companionGetAvatarImg(chatId);
    if (photo) {
        lv_obj_set_style_bg_color(wrap, lv_color_hex(0x202C33), 0);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
        lv_obj_t* img = lv_img_create(wrap);
        lv_img_set_src(img, photo);
        lv_obj_center(img);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    } else {
        // Fallback: circle container with green WhatsApp icon
        lv_obj_set_style_bg_color(wrap, lv_color_hex(0x25D366), 0);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
        lv_obj_t* ico = lv_img_create(wrap);
        lv_img_set_src(ico, &s_iconWhatsapp24Img);
        lv_obj_center(ico);
        lv_obj_clear_flag(ico, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    return wrap;
}

static void updateReplyStatusUi() {
    CompanionReplyState rs;
    companionGetReplyState(&rs);

    bool matchesActive = (rs.notifId[0] && strcmp(rs.notifId, s_activeId) == 0);
    if (!matchesActive || rs.status == CompanionReplyStatus::IDLE) {
        if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
        return;
    }

    switch (rs.status) {
        case CompanionReplyStatus::SENDING:
            if (s_sendBtn) lv_obj_add_state(s_sendBtn, LV_STATE_DISABLED);
            break;

        case CompanionReplyStatus::SENT:
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                if (s_replyTa) lv_textarea_set_text(s_replyTa, "");
                m3ShowToast("Message sent", 1100);
            }
            break;

        case CompanionReplyStatus::UNSUPPORTED:
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                m3ShowToast("Reply unavailable", 1500);
            }
            break;

        case CompanionReplyStatus::FAILED:
        default:
            if (s_sendBtn) lv_obj_clear_state(s_sendBtn, LV_STATE_DISABLED);
            if (rs.reqId != s_handledReq) {
                s_handledReq = rs.reqId;
                m3ShowToast("Send failed (text kept)", 1600);
            }
            break;
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
        m3ShowToast("Syncing chats...", 1100);
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
        m3ShowToast("Type a message first", 1100);
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
// Title Bar "?" Button (Right Side, Black Text)
// ============================================================================

static void installHeaderHelpBtn() {
    lv_obj_t* hdr = appManagerGetHeader();
    if (!hdr || s_helpBtn) return;

    s_helpBtn = lv_btn_create(hdr);
    lv_obj_set_size(s_helpBtn, 30, 30);
    lv_obj_align(s_helpBtn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_radius(s_helpBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_helpBtn, lv_color_hex(waHelpBtnBgHex()), 0);
    lv_obj_set_style_bg_opa(s_helpBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_helpBtn, 0, 0);
    lv_obj_set_style_pad_all(s_helpBtn, 0, 0);
    lv_obj_add_event_cb(s_helpBtn, openHelpModalCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lbl = lv_label_create(s_helpBtn);
    lv_label_set_text(lbl, "?");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
    lv_obj_center(lbl);
}

static void refreshHeaderHelpBtnTheme() {
    if (!s_helpBtn) return;
    lv_obj_set_style_bg_color(s_helpBtn, lv_color_hex(waHelpBtnBgHex()), 0);
    lv_obj_t* lbl = lv_obj_get_child(s_helpBtn, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
    }
}

// ============================================================================
// Views
// ============================================================================

static void renderInboxView() {
    if (!s_body) return;
    clearViewPtrs();
    lv_obj_clean(s_body);

    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 0, 0);

    // Compact connection bar (status + refresh + setup gear)
    lv_obj_t* topRow = lv_obj_create(s_body);
    lv_obj_set_size(topRow, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_hor(topRow, 8, 0);
    lv_obj_set_style_pad_ver(topRow, 3, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topRow, 4, 0);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    CompanionConnState st = companionGetState();
    uint8_t unread = companionUnreadCount();

    lv_obj_t* stChip = m3Card(topRow, 0, 24, 12);
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

    lv_obj_t* syncBtn = m3PillBtn(topRow, LV_SYMBOL_REFRESH, m3SurfaceHighestHex(), m3TextPrimaryHex(), 30, 24);
    lv_obj_add_event_cb(syncBtn, syncBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cfgBtn = m3PillBtn(topRow, LV_SYMBOL_SETTINGS, waAccentHex(), waBtnFgHex(), 32, 24);
    lv_obj_add_event_cb(cfgBtn, openSetupCb, LV_EVENT_CLICKED, nullptr);

    // Subtle hairline under top strip
    lv_obj_t* topDiv = lv_obj_create(s_body);
    lv_obj_set_size(topDiv, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(topDiv, lv_color_hex(waDividerHex()), 0);
    lv_obj_set_style_bg_opa(topDiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(topDiv, 0, 0);

    // WhatsApp Chat List container
    lv_obj_t* listCont = lv_obj_create(s_body);
    lv_obj_set_width(listCont, LV_PCT(100));
    lv_obj_set_flex_grow(listCont, 1);
    lv_obj_set_style_bg_opa(listCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listCont, 0, 0);
    lv_obj_set_style_pad_all(listCont, 0, 0);
    lv_obj_set_flex_flow(listCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listCont, 0, 0);
    lv_obj_set_scroll_dir(listCont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(listCont, LV_SCROLLBAR_MODE_AUTO);

    size_t count = companionGetMessageCount();
    if (count == 0) {
        lv_obj_t* padWrap = lv_obj_create(listCont);
        lv_obj_set_size(padWrap, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(padWrap, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(padWrap, 0, 0);
        lv_obj_set_style_pad_all(padWrap, 10, 0);
        lv_obj_clear_flag(padWrap, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* emptyCard = m3Card(padWrap, LV_PCT(100), LV_SIZE_CONTENT, 14);
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
            lv_label_set_text(title, "No WhatsApp chats yet");
            lv_label_set_text(sub, "Your WhatsApp chats will appear here as messages arrive on your phone.");
        }
        return;
    }

    // WhatsApp Android-style Chat List rows
    for (size_t i = 0; i < count; ++i) {
        CompanionMessage msg;
        if (!companionGetMessage(i, &msg)) continue;

        lv_obj_t* row = lv_obj_create(listCont);
        lv_obj_set_size(row, LV_PCT(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(m3CurrentBgHex()), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(m3SurfaceHighHex()), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, openMsgCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        // Left: Circular profile avatar (photo or circle container with green WhatsApp icon)
        lv_obj_t* avatar = createWaAvatar(row, msg.id, 38);
        lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, 0);

        // Contact / Group name
        lv_obj_t* senderLbl = lv_label_create(row);
        lv_label_set_text(senderLbl, msg.sender);
        lv_label_set_long_mode(senderLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(senderLbl, 134);
        lv_obj_set_style_text_font(senderLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(senderLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_align(senderLbl, LV_ALIGN_TOP_LEFT, 46, 3);

        // Timestamp (top right, green when unread just like WhatsApp Android)
        lv_obj_t* timeLbl = lv_label_create(row);
        lv_label_set_text(timeLbl, msg.timeStr[0] ? msg.timeStr : "");
        lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(timeLbl, lv_color_hex(msg.unread ? waAccentHex() : m3TextSecondaryHex()), 0);
        lv_obj_align(timeLbl, LV_ALIGN_TOP_RIGHT, 0, 4);

        // Last message preview (bottom left)
        bool hasBadge = msg.unread || (msg.unreadCount > 0);
        char prevBuf[COMPANION_TEXT_MAX + 8];
        if (msg.lastOutgoing && msg.text[0]) {
            snprintf(prevBuf, sizeof(prevBuf), LV_SYMBOL_OK " %s", msg.text);
        } else {
            snprintf(prevBuf, sizeof(prevBuf), "%s", msg.text[0] ? msg.text : "(no preview)");
        }
        lv_obj_t* prevLbl = lv_label_create(row);
        lv_label_set_text(prevLbl, prevBuf);
        lv_label_set_long_mode(prevLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(prevLbl, hasBadge ? 148 : 172);
        lv_obj_set_style_text_font(prevLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(prevLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_align(prevLbl, LV_ALIGN_BOTTOM_LEFT, 46, -3);

        // WhatsApp circular green unread count badge (bottom right)
        if (hasBadge) {
            uint8_t cnt = (msg.unreadCount > 0) ? msg.unreadCount : 1;
            lv_obj_t* badge = lv_obj_create(row);
            lv_obj_set_size(badge, cnt > 9 ? 20 : 16, 16);
            lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0x25D366), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(badge, 0, 0);
            lv_obj_set_style_pad_all(badge, 0, 0);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

            char cntBuf[6];
            snprintf(cntBuf, sizeof(cntBuf), "%u", (unsigned)cnt);
            lv_obj_t* bLbl = lv_label_create(badge);
            lv_label_set_text(bLbl, cntBuf);
            lv_obj_set_style_text_font(bLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(bLbl, lv_color_hex(g_sys.lightMode ? 0xFFFFFF : 0x062A14), 0);
            lv_obj_center(bLbl);
        }

        // Subtle WhatsApp chat list divider (inset past avatar)
        if (i + 1 < count) {
            lv_obj_t* div = lv_obj_create(listCont);
            lv_obj_set_size(div, LV_PCT(100), 1);
            lv_obj_set_style_bg_color(div, lv_color_hex(waDividerHex()), 0);
            lv_obj_set_style_bg_opa(div, LV_OPA_60, 0);
            lv_obj_set_style_border_width(div, 0, 0);
        }
    }
}

static void addChatBubble(lv_obj_t* parent, const char* chatTitle,
                          const char* sender, const char* text,
                          const char* timeStr, bool outgoing) {
    // Full-width row wrapper to align bubble left (incoming) or right (outgoing)
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // WhatsApp message bubble
    lv_obj_t* bubble = lv_obj_create(row);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, 184, 0);
    lv_obj_set_style_min_width(bubble, 62, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(outgoing ? waBubbleOutBgHex() : waBubbleInBgHex()), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_pad_hor(bubble, 8, 0);
    lv_obj_set_style_pad_ver(bubble, 5, 0);
    lv_obj_set_style_shadow_width(bubble, g_sys.lightMode ? 2 : 0, 0);
    lv_obj_set_style_shadow_opa(bubble, LV_OPA_10, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bubble, 2, 0);

    // In group chats, show sender name at top of incoming bubble in WhatsApp accent green
    if (!outgoing && sender && sender[0] && chatTitle && strcmp(sender, chatTitle) != 0) {
        lv_obj_t* sLbl = lv_label_create(bubble);
        lv_label_set_text(sLbl, sender);
        lv_label_set_long_mode(sLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_width(sLbl, 166, 0);
        lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sLbl, lv_color_hex(waAccentHex()), 0);
    }

    // Message body text
    lv_obj_t* tLbl = lv_label_create(bubble);
    lv_label_set_text(tLbl, (text && text[0]) ? text : "(No message)");
    lv_label_set_long_mode(tLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(tLbl, 166, 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(waBubbleTextHex()), 0);

    // Bottom-right meta row: timestamp (+ blue checkmark for outgoing)
    lv_obj_t* meta = lv_obj_create(bubble);
    lv_obj_set_size(meta, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta, 3, 0);

    if (timeStr && timeStr[0]) {
        lv_obj_t* tm = lv_label_create(meta);
        lv_label_set_text(tm, timeStr);
        lv_obj_set_style_text_font(tm, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(tm, lv_color_hex(waBubbleTimeHex()), 0);
    }
    if (outgoing) {
        lv_obj_t* chk = lv_label_create(meta);
        lv_label_set_text(chk, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(chk, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(chk, lv_color_hex(0x53BDEB), 0);
    }
}

static void populateChatBubbles(const CompanionMessage& chat) {
    if (!s_chatScroll) return;
    lv_obj_clean(s_chatScroll);

    size_t bCount = companionGetChatBubbleCount(chat.id);
    if (bCount == 0) {
        addChatBubble(s_chatScroll, chat.sender, chat.sender, chat.text, chat.timeStr, chat.lastOutgoing);
        s_renderedBubbles = 1;
    } else {
        for (size_t i = 0; i < bCount; ++i) {
            CompanionChatBubble b;
            if (companionGetChatBubble(chat.id, i, &b)) {
                addChatBubble(s_chatScroll, chat.sender, b.sender, b.text, b.timeStr, b.outgoing);
            }
        }
        s_renderedBubbles = bCount;
    }

    // Scroll to newest message at the bottom (just like WhatsApp Android)
    lv_obj_update_layout(s_chatScroll);
    lv_obj_scroll_to_y(s_chatScroll, LV_COORD_MAX, LV_ANIM_OFF);
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

    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 0, 0);

    // WhatsApp Android Chat Top Bar: < [Avatar] Contact Name [Dismiss]
    lv_obj_t* hdr = lv_obj_create(s_body);
    lv_obj_set_size(hdr, LV_PCT(100), 34);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(waChatHeaderBgHex()), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 6, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = lv_btn_create(hdr);
    lv_obj_set_size(backBtn, 26, 26);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(backBtn, 0, 0);
    lv_obj_set_style_pad_all(backBtn, 0, 0);
    lv_obj_add_event_cb(backBtn, backToInboxCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_label_set_text(backLbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(backLbl, lv_color_hex(waChatHeaderFgHex()), 0);
    lv_obj_center(backLbl);

    createWaAvatar(hdr, msg.id, 26);

    lv_obj_t* nameLbl = lv_label_create(hdr);
    lv_label_set_text(nameLbl, msg.sender);
    lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(nameLbl, 1);
    lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(nameLbl, lv_color_hex(waChatHeaderFgHex()), 0);

    lv_obj_t* disBtn = lv_btn_create(hdr);
    lv_obj_set_size(disBtn, 26, 26);
    lv_obj_set_style_bg_opa(disBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(disBtn, 0, 0);
    lv_obj_set_style_pad_all(disBtn, 0, 0);
    lv_obj_add_event_cb(disBtn, dismissBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* disLbl = lv_label_create(disBtn);
    lv_label_set_text(disLbl, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(disLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(disLbl, lv_color_hex(waChatHeaderFgHex()), 0);
    lv_obj_set_style_text_opa(disLbl, LV_OPA_80, 0);
    lv_obj_center(disLbl);

    // WhatsApp Chat Wallpaper & Scrollable Bubbles Area
    s_chatScroll = lv_obj_create(s_body);
    lv_obj_set_width(s_chatScroll, LV_PCT(100));
    lv_obj_set_flex_grow(s_chatScroll, 1);
    lv_obj_set_style_bg_color(s_chatScroll, lv_color_hex(waChatWallpaperHex()), 0);
    lv_obj_set_style_bg_opa(s_chatScroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chatScroll, 0, 0);
    lv_obj_set_style_radius(s_chatScroll, 0, 0);
    lv_obj_set_style_pad_hor(s_chatScroll, 8, 0);
    lv_obj_set_style_pad_ver(s_chatScroll, 6, 0);
    lv_obj_set_flex_flow(s_chatScroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_chatScroll, 5, 0);
    lv_obj_set_scroll_dir(s_chatScroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chatScroll, LV_SCROLLBAR_MODE_AUTO);

    populateChatBubbles(msg);

    // WhatsApp Android Bottom Composer Bar
    lv_obj_t* compBar = lv_obj_create(s_body);
    lv_obj_set_size(compBar, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(compBar, lv_color_hex(waChatWallpaperHex()), 0);
    lv_obj_set_style_bg_opa(compBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(compBar, 0, 0);
    lv_obj_set_style_radius(compBar, 0, 0);
    lv_obj_set_style_pad_hor(compBar, 6, 0);
    lv_obj_set_style_pad_ver(compBar, 4, 0);
    lv_obj_set_flex_flow(compBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(compBar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(compBar, 6, 0);
    lv_obj_clear_flag(compBar, LV_OBJ_FLAG_SCROLLABLE);

    if (msg.canReply) {
        s_replyTa = lv_textarea_create(compBar);
        lv_obj_set_height(s_replyTa, 30);
        lv_obj_set_flex_grow(s_replyTa, 1);
        lv_textarea_set_one_line(s_replyTa, true);
        lv_textarea_set_max_length(s_replyTa, 180);
        lv_textarea_set_placeholder_text(s_replyTa, "Message");
        styleComposerTextarea(s_replyTa);
        m3KeyboardAttach(s_replyTa, onReplyEnter);

        // Circular green WhatsApp Send button
        s_sendBtn = lv_btn_create(compBar);
        lv_obj_set_size(s_sendBtn, 30, 30);
        lv_obj_set_style_radius(s_sendBtn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_sendBtn, lv_color_hex(0x00A884), 0);
        lv_obj_set_style_bg_opa(s_sendBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(s_sendBtn, 0, 0);
        lv_obj_set_style_pad_all(s_sendBtn, 0, 0);
        lv_obj_add_event_cb(s_sendBtn, sendBtnCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* sendIco = lv_label_create(s_sendBtn);
        lv_label_set_text(sendIco, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(sendIco, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sendIco, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(sendIco);

        updateReplyStatusUi();
    } else {
        lv_obj_t* noteLbl = lv_label_create(compBar);
        lv_label_set_text(noteLbl, "Reply unavailable for this chat");
        lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(noteLbl, lv_color_hex(waBubbleTimeHex()), 0);
    }
}

static void renderSetupView() {
    if (!s_body) return;
    clearViewPtrs();
    lv_obj_clean(s_body);

    lv_obj_set_style_pad_all(s_body, M3Dim::PAD_MD, 0);
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

    // Buttons: Back | Connect | Off
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
        CompanionMessage chat;
        if (companionFindMessageById(s_activeId, &chat)) {
            size_t bCount = companionGetChatBubbleCount(s_activeId);
            if (bCount != s_renderedBubbles) {
                populateChatBubbles(chat);
            }
        }
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
    refreshHeaderHelpBtnTheme();
    if (s_view == WaView::INBOX) {
        renderInboxView();
    } else if (s_view == WaView::DETAIL) {
        // Re-skin composer in place to preserve unsaved reply text
        styleComposerTextarea(s_replyTa);
    } else if (s_view == WaView::SETUP) {
        styleTextarea(s_hostTa);
        styleTextarea(s_portTa);
        styleTextarea(s_tokenTa);
    }
}

static void buildAppWhatsapp(lv_obj_t* body) {
    s_body = body;
    s_helpBtn = nullptr;
    s_view = WaView::INBOX;
    s_activeId[0] = '\0';
    s_lastGen = companionGetDataGen();
    s_lastState = companionGetState();
    s_handledReq = 0;
    clearViewPtrs();
    lv_obj_add_event_cb(body, waDeleteCb, LV_EVENT_DELETE, nullptr);
    installHeaderHelpBtn();
    renderInboxView();
}

REGISTER_APP_THEMED_IMG(whatsapp, "whatsapp", "WhatsApp", LV_SYMBOL_ENVELOPE, &s_iconWhatsappImg,
                        0x25D366, 0x103923, buildAppWhatsapp, tickAppWhatsapp, refreshAppWhatsappTheme, 60);
