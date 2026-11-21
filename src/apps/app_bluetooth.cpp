#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../ui/status_bar.h"
#include <string.h>

struct BtDev {
    const char* name;
    const char* type;
};

static const BtDev s_devs[3] = {
    { "Pixel Buds Pro", "Audio • 92% Battery" },
    { "Pixel Watch 2",  "Wearable • Synced" },
    { "Car Audio",      "Hands-free • Saved" }
};

static void btSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_sys.btEnabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (g_sys.btEnabled) g_sys.airplaneMode = false;
    statusBarRefresh();
}

static void devClickCb(lv_event_t* e) {
    const char* dev = (const char*)lv_event_get_user_data(e);
    m3PulseFeedback();
    if (!g_sys.btEnabled) {
        m3ShowToast("Enable Bluetooth first", 1100);
        return;
    }
    strncpy(g_sys.btDevice, dev, sizeof(g_sys.btDevice) - 1);
    g_sys.btDevice[sizeof(g_sys.btDevice) - 1] = '\0';
    statusBarRefresh();
    char msg[48];
    snprintf(msg, sizeof(msg), "Active: %s", dev);
    m3ShowToast(msg, 1200);
}

void buildAppBluetooth(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);

    m3ToggleRow(body, LV_SYMBOL_BLUETOOTH, "Bluetooth", "Visible as Pixel 8 CYD", g_sys.btEnabled, M3Color::SECONDARY, btSwCb);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t* card = m3Card(body, LV_PCT(100), 46, 12);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, devClickCb, LV_EVENT_CLICKED, (void*)s_devs[i].name);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(ico, lv_color_hex(M3Color::SECONDARY), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, s_devs[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 24, -1);

        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, s_devs[i].type);
        lv_obj_set_style_text_color(sub, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 24, 1);
    }
}

REGISTER_APP(bluetooth, "bluetooth", "Bluetooth", LV_SYMBOL_BLUETOOTH, 0xC5B4E3, 0x362B4E, buildAppBluetooth, nullptr, 50);
