#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../ui/status_bar.h"
#include <string.h>

struct WifiNet {
    const char* ssid;
    const char* sec;
    int8_t rssi;
};

static const WifiNet s_nets[4] = {
    { "Pixel-Home-5G",   "WPA3 • Connected", -44 },
    { "Studio_Mesh",     "WPA2",             -58 },
    { "CoffeeLab_Guest", "Open",             -71 },
    { "Neighbor_2.4G",   "WPA2",             -82 }
};

static lv_obj_t* s_body = nullptr;

static void wifiSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_sys.wifiEnabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (g_sys.wifiEnabled) g_sys.airplaneMode = false;
    statusBarRefresh();
}

static void netClickCb(lv_event_t* e) {
    const char* ssid = (const char*)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (!g_sys.wifiEnabled) {
        m3ShowToast("Enable Wi-Fi first", 1100);
        return;
    }
    strncpy(g_sys.wifiSsid, ssid, sizeof(g_sys.wifiSsid) - 1);
    g_sys.wifiSsid[sizeof(g_sys.wifiSsid) - 1] = '\0';
    statusBarRefresh();
    char msg[48];
    snprintf(msg, sizeof(msg), "Connected to %s", ssid);
    m3ShowToast(msg, 1200);
}

void buildAppWifi(lv_obj_t* body) {
    s_body = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);

    m3ToggleRow(body, LV_SYMBOL_WIFI, "Use Wi-Fi", "802.11 b/g/n", g_sys.wifiEnabled, M3Color::PRIMARY, wifiSwCb);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t* card = m3Card(body, LV_PCT(100), 42, 12);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, netClickCb, LV_EVENT_CLICKED, (void*)s_nets[i].ssid);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(ico, lv_color_hex(
            strcmp(s_nets[i].ssid, g_sys.wifiSsid) == 0 ? M3Color::PRIMARY : M3Color::TEXT_SECONDARY), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, s_nets[i].ssid);
        lv_obj_set_style_text_color(name, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 24, -2);

        char rssiBuf[32];
        snprintf(rssiBuf, sizeof(rssiBuf), "%s  (%d dBm)", s_nets[i].sec, s_nets[i].rssi);
        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, rssiBuf);
        lv_obj_set_style_text_color(sub, lv_color_hex(M3Color::TEXT_MUTED), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 24, 2);
    }
}

REGISTER_APP(wifi, "wifi", "Wi-Fi", LV_SYMBOL_WIFI, 0x8AB4F8, 0x1A3252, buildAppWifi, nullptr, 40);
