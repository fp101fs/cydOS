#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include "../drivers/http_reader.h"
#include <stdio.h>
#include <string.h>

struct Bookmark {
    const char* name;
    const char* url;
};

static constexpr int BOOKMARK_COUNT = 4;

static const Bookmark s_sites[BOOKMARK_COUNT] = {
    { "Example", "https://example.com" },
    { "CERN",    "http://info.cern.ch" },
    { "HN",      "https://news.ycombinator.com" },
    { "Lite",    "https://lite.cnn.com" }
};

enum class StatusTone : uint8_t {
    MUTED = 0,
    LOADING,
    OK,
    ERROR
};

static lv_obj_t* s_urlTa = nullptr;
static lv_obj_t* s_goBtn = nullptr;
static lv_obj_t* s_goIcon = nullptr;
static lv_obj_t* s_chips[BOOKMARK_COUNT] = {};
static lv_obj_t* s_statusLbl = nullptr;
static lv_obj_t* s_pageCard = nullptr;
static lv_obj_t* s_pageTitle = nullptr;
static lv_obj_t* s_pageBody = nullptr;

static StatusTone s_statusTone = StatusTone::MUTED;
static uint32_t s_activeGen = 0;
static HttpReaderState s_renderedState = HttpReaderState::IDLE;
static bool s_lastWifiConnected = false;
static HttpReaderResult s_snap = {};

static uint32_t statusColorHex(StatusTone tone) {
    switch (tone) {
        case StatusTone::LOADING: return m3PrimaryHex();
        case StatusTone::OK:      return g_sys.lightMode ? 0x1E7E34 : M3Color::TERTIARY;
        case StatusTone::ERROR:   return g_sys.lightMode ? 0xC62828 : M3Color::CORAL;
        default:                  return m3TextSecondaryHex();
    }
}

static void setStatus(const char* text, StatusTone tone) {
    s_statusTone = tone;
    if (s_statusLbl) {
        lv_label_set_text(s_statusLbl, text ? text : "");
        lv_obj_set_style_text_color(s_statusLbl, lv_color_hex(statusColorHex(tone)), 0);
    }
}

static void setGoBtnLoading(bool loading) {
    if (s_goIcon) {
        lv_label_set_text(s_goIcon, loading ? LV_SYMBOL_CLOSE : LV_SYMBOL_REFRESH);
    }
}

static void refreshAppBrowserTheme() {
    if (s_urlTa) {
        lv_obj_set_style_bg_color(s_urlTa, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_text_color(s_urlTa, lv_color_hex(g_sys.lightMode ? m3PrimaryHex() : M3Color::CYAN), 0);
        lv_obj_set_style_border_color(s_urlTa, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_goBtn) {
        lv_obj_set_style_bg_color(s_goBtn, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_goIcon) {
        lv_obj_set_style_text_color(s_goIcon, lv_color_hex(g_sys.lightMode ? 0xFFFFFF : M3Color::ON_PRIMARY), 0);
    }
    for (int i = 0; i < BOOKMARK_COUNT; ++i) {
        if (s_chips[i]) {
            lv_obj_set_style_bg_color(s_chips[i], lv_color_hex(m3SurfaceHighestHex()), 0);
            lv_obj_t* lbl = lv_obj_get_child(s_chips[i], 0);
            if (lbl) {
                lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
            }
        }
    }
    if (s_statusLbl) {
        lv_obj_set_style_text_color(s_statusLbl, lv_color_hex(statusColorHex(s_statusTone)), 0);
    }
    if (s_pageCard) {
        lv_obj_set_style_bg_color(s_pageCard, lv_color_hex(m3SurfaceHex()), 0);
        lv_obj_set_style_border_color(s_pageCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_pageTitle) {
        lv_obj_set_style_text_color(s_pageTitle, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_pageBody) {
        lv_obj_set_style_text_color(s_pageBody, lv_color_hex(m3TextSecondaryHex()), 0);
    }
}

static void renderResult(const HttpReaderResult& res) {
    if (res.url[0] && s_urlTa) {
        lv_textarea_set_text(s_urlTa, res.url);
    }

    if (res.state == HttpReaderState::LOADING) {
        setGoBtnLoading(true);
        char status[96];
        snprintf(status, sizeof(status), "Loading %s...", res.title[0] ? res.title : res.url);
        setStatus(status, StatusTone::LOADING);
        if (s_pageTitle) lv_label_set_text(s_pageTitle, res.title[0] ? res.title : "Loading...");
        if (s_pageBody) lv_label_set_text(s_pageBody, "Fetching page over Wi-Fi...");
        if (s_pageCard) lv_obj_scroll_to_y(s_pageCard, 0, LV_ANIM_OFF);
        return;
    }

    setGoBtnLoading(false);

    if (res.state == HttpReaderState::DONE) {
        char status[96];
        if (res.rawBytes >= 1024) {
            snprintf(status, sizeof(status), "%d OK • %.1f KB • %lu ms%s",
                     res.httpCode,
                     (double)res.rawBytes / 1024.0,
                     (unsigned long)res.elapsedMs,
                     res.truncated ? " (trim)" : "");
        } else {
            snprintf(status, sizeof(status), "%d OK • %u B • %lu ms%s",
                     res.httpCode,
                     (unsigned)res.rawBytes,
                     (unsigned long)res.elapsedMs,
                     res.truncated ? " (trim)" : "");
        }
        setStatus(status, StatusTone::OK);
        if (s_pageTitle) lv_label_set_text(s_pageTitle, res.title[0] ? res.title : res.url);
        if (s_pageBody) lv_label_set_text(s_pageBody, res.body[0] ? res.body : "(Empty page)");
        if (s_pageCard) lv_obj_scroll_to_y(s_pageCard, 0, LV_ANIM_OFF);
        return;
    }

    if (res.state == HttpReaderState::ERROR) {
        char status[112];
        snprintf(status, sizeof(status), "Error • %s", res.error[0] ? res.error : "Request failed");
        setStatus(status, StatusTone::ERROR);
        if (s_pageTitle) lv_label_set_text(s_pageTitle, res.title[0] ? res.title : "Unable to load");
        if (s_pageBody) {
            if (res.body[0]) {
                lv_label_set_text(s_pageBody, res.body);
            } else if (!g_sys.wifiConnected) {
                lv_label_set_text(s_pageBody,
                    "No Wi-Fi connection.\n\nOpen the Wi-Fi app to connect to a 2.4 GHz network, then retry.");
            } else {
                char msg[192];
                snprintf(msg, sizeof(msg), "Could not load:\n%s\n\n%s",
                         res.url[0] ? res.url : "(empty URL)",
                         res.error[0] ? res.error : "Unknown error");
                lv_label_set_text(s_pageBody, msg);
            }
        }
        if (s_pageCard) lv_obj_scroll_to_y(s_pageCard, 0, LV_ANIM_OFF);
    }
}

static void startFetchUrl(const char* rawUrl) {
    if (!rawUrl || !rawUrl[0]) return;
    if (m3KeyboardIsOpen()) m3KeyboardHide();

    s_activeGen = httpReaderFetch(rawUrl);

    if (httpReaderGetResult(&s_snap)) {
        s_renderedState = s_snap.state;
        renderResult(s_snap);
    }
}

static void browserDeleteCb(lv_event_t* e) {
    (void)e;
    if (httpReaderIsLoading()) {
        httpReaderCancel();
    }
    s_urlTa = nullptr;
    s_goBtn = nullptr;
    s_goIcon = nullptr;
    for (int i = 0; i < BOOKMARK_COUNT; ++i) s_chips[i] = nullptr;
    s_statusLbl = nullptr;
    s_pageCard = nullptr;
    s_pageTitle = nullptr;
    s_pageBody = nullptr;
    s_activeGen = 0;
    s_renderedState = HttpReaderState::IDLE;
}

static void onUrlEnter(const char* text) {
    if (!text || !text[0]) return;
    startFetchUrl(text);
}

static void goBtnClickCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (httpReaderIsLoading()) {
        httpReaderCancel();
        s_renderedState = HttpReaderState::IDLE;
        setGoBtnLoading(false);
        setStatus("Cancelled", StatusTone::MUTED);
        if (s_pageBody) lv_label_set_text(s_pageBody, "Request cancelled.");
        return;
    }
    if (s_urlTa) {
        const char* url = lv_textarea_get_text(s_urlTa);
        if (url && url[0]) startFetchUrl(url);
    }
}

static void siteClickCb(lv_event_t* e) {
    m3PulseFeedback();
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= BOOKMARK_COUNT) return;
    if (s_urlTa) lv_textarea_set_text(s_urlTa, s_sites[idx].url);
    startFetchUrl(s_sites[idx].url);
}

static void updateIdleHint() {
    if (g_sys.wifiConnected) {
        setStatus("Wi-Fi ready • Tap bookmark or URL", StatusTone::MUTED);
    } else {
        setStatus("Offline • Connect Wi-Fi to browse", StatusTone::MUTED);
    }
}

void tickAppBrowser() {
    if (!s_pageCard) return;

    if (s_activeGen != 0) {
        HttpReaderState curState = httpReaderGetState();
        uint32_t curGen = httpReaderGetGen();
        if (curGen == s_activeGen && curState != s_renderedState) {
            if (httpReaderGetResult(&s_snap)) {
                s_renderedState = s_snap.state;
                renderResult(s_snap);
            }
        }
    } else if (s_lastWifiConnected != g_sys.wifiConnected) {
        s_lastWifiConnected = g_sys.wifiConnected;
        updateIdleHint();
    }
}

void buildAppBrowser(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 5, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(body, browserDeleteCb, LV_EVENT_DELETE, nullptr);

    s_activeGen = 0;
    s_renderedState = HttpReaderState::IDLE;
    s_lastWifiConnected = g_sys.wifiConnected;

    // Row 1: URL input bar + Go/Cancel button
    lv_obj_t* urlRow = lv_obj_create(body);
    lv_obj_set_size(urlRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(urlRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(urlRow, 0, 0);
    lv_obj_set_style_pad_all(urlRow, 0, 0);
    lv_obj_set_flex_flow(urlRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(urlRow, 4, 0);
    lv_obj_clear_flag(urlRow, LV_OBJ_FLAG_SCROLLABLE);

    s_urlTa = lv_textarea_create(urlRow);
    lv_obj_set_size(s_urlTa, 182, 30);
    lv_obj_set_flex_grow(s_urlTa, 1);
    lv_textarea_set_one_line(s_urlTa, true);
    lv_textarea_set_text(s_urlTa, s_sites[0].url);
    lv_obj_set_style_text_font(s_urlTa, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_width(s_urlTa, 1, 0);
    lv_obj_set_style_radius(s_urlTa, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(s_urlTa, 10, 0);
    lv_obj_set_style_pad_ver(s_urlTa, 5, 0);
    m3KeyboardAttach(s_urlTa, onUrlEnter);

    s_goBtn = lv_btn_create(urlRow);
    lv_obj_set_size(s_goBtn, 34, 30);
    lv_obj_set_style_bg_opa(s_goBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_goBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(s_goBtn, 0, 0);
    lv_obj_add_event_cb(s_goBtn, goBtnClickCb, LV_EVENT_CLICKED, nullptr);

    s_goIcon = lv_label_create(s_goBtn);
    lv_label_set_text(s_goIcon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(s_goIcon, &lv_font_montserrat_12, 0);
    lv_obj_center(s_goIcon);

    // Row 2: Quick bookmark chips
    lv_obj_t* chipRow = lv_obj_create(body);
    lv_obj_set_size(chipRow, LV_PCT(100), 26);
    lv_obj_set_style_bg_opa(chipRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chipRow, 0, 0);
    lv_obj_set_style_pad_all(chipRow, 0, 0);
    lv_obj_set_flex_flow(chipRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chipRow, 4, 0);
    lv_obj_clear_flag(chipRow, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < BOOKMARK_COUNT; ++i) {
        s_chips[i] = m3PillBtn(chipRow, s_sites[i].name, m3SurfaceHighestHex(), m3TextPrimaryHex(), 52, 24);
        lv_obj_add_event_cb(s_chips[i], siteClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    // Row 3: Live status strip
    s_statusLbl = lv_label_create(body);
    lv_obj_set_width(s_statusLbl, LV_PCT(100));
    lv_label_set_long_mode(s_statusLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_statusLbl, &lv_font_montserrat_10, 0);

    // Row 4: Scrollable reader card
    s_pageCard = m3Card(body, LV_PCT(100), 100, 14);
    lv_obj_set_flex_grow(s_pageCard, 1);
    lv_obj_set_style_pad_all(s_pageCard, 8, 0);
    lv_obj_set_flex_flow(s_pageCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pageCard, 4, 0);
    lv_obj_add_flag(s_pageCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_pageCard, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_pageCard, LV_SCROLLBAR_MODE_AUTO);

    s_pageTitle = lv_label_create(s_pageCard);
    lv_obj_set_width(s_pageTitle, LV_PCT(100));
    lv_label_set_long_mode(s_pageTitle, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_pageTitle, "Pixel Web Reader");
    lv_obj_set_style_text_font(s_pageTitle, &lv_font_montserrat_14, 0);

    s_pageBody = lv_label_create(s_pageCard);
    lv_obj_set_width(s_pageBody, LV_PCT(100));
    lv_label_set_long_mode(s_pageBody, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_pageBody,
        "HTTP/HTTPS text reader over Wi-Fi.\n\nTap a bookmark above or enter any URL and press Go.");
    lv_obj_set_style_text_font(s_pageBody, &lv_font_montserrat_12, 0);

    updateIdleHint();
    refreshAppBrowserTheme();
}

REGISTER_APP_THEMED(browser, "browser", "Browser", LV_SYMBOL_GPS, 0x78D9EC, 0x0284C7, buildAppBrowser, tickAppBrowser, refreshAppBrowserTheme, 90);
