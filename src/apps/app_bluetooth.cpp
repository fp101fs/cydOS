#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../drivers/ble_mgr.h"
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
static lv_obj_t* s_clearBtn = nullptr;
static lv_obj_t* s_listBox = nullptr;

static uint32_t s_lastScanGen = 0xFFFFFFFFUL;
static bool     s_lastScanning = false;
static bool     s_lastEnabled = false;
static size_t   s_lastCount = 0;

static void updateHeaderAndControls();
static void rebuildDeviceList();

static void btSwCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    m3PulseFeedback();
    bleMgrSetEnabled(en);
    if (en) {
        bleMgrStartScan();
    }
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
    updateHeaderAndControls();
    rebuildDeviceList();
}

static void scanBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (!g_sys.btEnabled || g_sys.airplaneMode) {
        m3ShowToast("Enable Bluetooth first", 1100);
        return;
    }
    if (bleMgrIsScanning()) {
        bleMgrStopScan();
        m3ShowToast("BLE scan stopped", 1000);
    } else if (!bleMgrStartScan()) {
        m3ShowToast("BLE scan unavailable", 1200);
    }
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
    updateHeaderAndControls();
    rebuildDeviceList();
}

static void clearBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    bleMgrClearResults();
    if (quickSettingsIsOpen()) quickSettingsRefresh();
    updateHeaderAndControls();
    rebuildDeviceList();
}

static void devClickCb(lv_event_t* e) {
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const BleScanEntry* dev = bleMgrGetScanResult(idx);
    if (!dev) return;
    m3PulseFeedback();

    if (!g_sys.btEnabled || g_sys.airplaneMode) {
        m3ShowToast("Enable Bluetooth first", 1100);
        return;
    }

    const char* label = dev->hasName ? dev->name : dev->addr;
    strncpy(g_sys.btDevice, label, sizeof(g_sys.btDevice) - 1);
    g_sys.btDevice[sizeof(g_sys.btDevice) - 1] = '\0';
    statusBarRefresh();
    if (quickSettingsIsOpen()) quickSettingsRefresh();

    char msg[72];
    if (dev->hasName) {
        snprintf(msg, sizeof(msg), "%s\n%s (%d dBm)", dev->name, dev->addr, (int)dev->rssi);
    } else {
        snprintf(msg, sizeof(msg), "%s (%d dBm)", dev->addr, (int)dev->rssi);
    }
    m3ShowToast(msg, 1800);
}

static void updateHeaderAndControls() {
    if (!s_body) return;

    bool active = g_sys.btEnabled && !g_sys.airplaneMode;
    bool scanning = bleMgrIsScanning();
    size_t count = bleMgrGetScanCount();

    if (s_toggleSw) {
        if (active) lv_obj_add_state(s_toggleSw, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_toggleSw, LV_STATE_CHECKED);
    }

    if (s_toggleSub) {
        char sub[48];
        if (!active) {
            snprintf(sub, sizeof(sub), "Off");
        } else if (scanning) {
            snprintf(sub, sizeof(sub), "Scanning BLE (%lus)...", (unsigned long)BLE_SCAN_DURATION_SEC);
        } else if (count > 0) {
            snprintf(sub, sizeof(sub), "%u BLE device%s nearby",
                     (unsigned)count, (count == 1) ? "" : "s");
        } else {
            snprintf(sub, sizeof(sub), "BLE Scanner Ready");
        }
        lv_label_set_text(s_toggleSub, sub);
    }

    if (s_scanBtnLbl) {
        lv_label_set_text(s_scanBtnLbl, scanning ? LV_SYMBOL_CLOSE "  Stop" : LV_SYMBOL_REFRESH "  Scan");
    }

    if (s_clearBtn) {
        if (count > 0 && !scanning) lv_obj_clear_flag(s_clearBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_clearBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuildDeviceList() {
    if (!s_listBox) return;
    lv_obj_clean(s_listBox);

    if (!g_sys.btEnabled || g_sys.airplaneMode) {
        lv_obj_t* msg = lv_label_create(s_listBox);
        lv_label_set_text(msg, "Turn on Bluetooth to scan nearby BLE devices");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_set_width(msg, LV_PCT(100));
        return;
    }

    size_t count = bleMgrGetScanCount();
    if (count == 0) {
        lv_obj_t* msg = lv_label_create(s_listBox);
        lv_label_set_text(msg, bleMgrIsScanning()
            ? "Scanning nearby BLE devices..."
            : "No BLE devices found. Tap Scan.");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_set_width(msg, LV_PCT(100));
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const BleScanEntry* dev = bleMgrGetScanResult(i);
        if (!dev) continue;

        lv_obj_t* card = m3Card(s_listBox, LV_PCT(100), 46, 12);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 6, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, devClickCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        int8_t bars = bleMgrRssiToBars(dev->rssi);
        uint32_t icoCol = (bars >= 3) ? M3Color::TERTIARY :
                          (bars == 2) ? M3Color::SECONDARY : m3TextSecondaryHex();

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(ico, lv_color_hex(icoCol), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_12, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, dev->hasName ? dev->name : "Unnamed BLE");
        lv_obj_set_style_text_color(name, lv_color_hex(dev->hasName ? m3TextPrimaryHex() : m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_width(name, 172);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, -1);

        char subBuf[48];
        snprintf(subBuf, sizeof(subBuf), "%s  •  %d dBm", dev->addr, (int)dev->rssi);

        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, subBuf);
        lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
        lv_obj_set_width(sub, 174);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 22, 1);
    }
}

static void refreshAppBluetoothTheme() {
    if (!s_body) return;
    if (s_toggleRow) {
        lv_obj_set_style_bg_color(s_toggleRow, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_toggleRow, lv_color_hex(m3OutlineHex()), 0);
        lv_obj_t* title = lv_obj_get_child(s_toggleRow, 1);
        if (title) lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        if (s_toggleSub) lv_obj_set_style_text_color(s_toggleSub, lv_color_hex(m3TextSecondaryHex()), 0);
    }
    if (s_clearBtn) {
        lv_obj_set_style_bg_color(s_clearBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
        lv_obj_t* lbl = lv_obj_get_child(s_clearBtn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    rebuildDeviceList();
}

static void bluetoothDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    s_toggleRow = nullptr;
    s_toggleSub = nullptr;
    s_toggleSw = nullptr;
    s_scanBtn = nullptr;
    s_scanBtnLbl = nullptr;
    s_clearBtn = nullptr;
    s_listBox = nullptr;
}

void buildAppBluetooth(lv_obj_t* body) {
    s_body = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, bluetoothDeleteCb, LV_EVENT_DELETE, nullptr);

    s_toggleRow = m3ToggleRow(body, LV_SYMBOL_BLUETOOTH, "Bluetooth", "BLE Scanner Ready",
                              g_sys.btEnabled && !g_sys.airplaneMode, M3Color::SECONDARY, btSwCb);
    // m3ToggleRow child order with icon+subtitle: 0=ico, 1=title, 2=subtitle, 3=switch
    s_toggleSub = lv_obj_get_child(s_toggleRow, 2);
    s_toggleSw  = lv_obj_get_child(s_toggleRow, 3);

    // Action row: Scan/Stop + Clear
    lv_obj_t* actRow = lv_obj_create(body);
    lv_obj_set_size(actRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(actRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actRow, 0, 0);
    lv_obj_set_style_pad_all(actRow, 0, 0);
    lv_obj_set_flex_flow(actRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actRow, 6, 0);
    lv_obj_clear_flag(actRow, LV_OBJ_FLAG_SCROLLABLE);

    s_scanBtn = m3PillBtn(actRow, LV_SYMBOL_REFRESH "  Scan", M3Color::SECONDARY_CONT, M3Color::SECONDARY, 108, 28);
    s_scanBtnLbl = lv_obj_get_child(s_scanBtn, 0);
    lv_obj_add_event_cb(s_scanBtn, scanBtnCb, LV_EVENT_CLICKED, nullptr);

    s_clearBtn = m3PillBtn(actRow, LV_SYMBOL_TRASH "  Clear", m3SurfaceHighestHex(), m3TextPrimaryHex(), 96, 28);
    lv_obj_add_event_cb(s_clearBtn, clearBtnCb, LV_EVENT_CLICKED, nullptr);

    // Device list container
    s_listBox = lv_obj_create(body);
    lv_obj_set_size(s_listBox, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_listBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_listBox, 0, 0);
    lv_obj_set_style_pad_all(s_listBox, 0, 0);
    lv_obj_set_flex_flow(s_listBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_listBox, 6, 0);
    lv_obj_clear_flag(s_listBox, LV_OBJ_FLAG_SCROLLABLE);

    // Auto-start async BLE scan on open if enabled and no cached results yet
    if (g_sys.btEnabled && !g_sys.airplaneMode && !bleMgrIsScanning() && bleMgrGetScanCount() == 0) {
        bleMgrStartScan();
    }

    s_lastScanGen = bleMgrGetScanGen();
    s_lastScanning = bleMgrIsScanning();
    s_lastEnabled = g_sys.btEnabled && !g_sys.airplaneMode;
    s_lastCount = bleMgrGetScanCount();

    updateHeaderAndControls();
    refreshAppBluetoothTheme();
}

void tickAppBluetooth() {
    if (!s_body) return;

    BleEvent ev = bleMgrConsumeEvent();
    if (ev == BleEvent::SCAN_DONE) {
        char msg[40];
        size_t n = bleMgrGetScanCount();
        snprintf(msg, sizeof(msg), "Found %u BLE device%s", (unsigned)n, (n == 1) ? "" : "s");
        m3ShowToast(msg, 1200);
    } else if (ev == BleEvent::SCAN_FAILED) {
        m3ShowToast("BLE scan failed", 1300);
    }

    uint32_t gen = bleMgrGetScanGen();
    bool scanning = bleMgrIsScanning();
    bool enabled = g_sys.btEnabled && !g_sys.airplaneMode;
    size_t count = bleMgrGetScanCount();

    if (gen != s_lastScanGen || scanning != s_lastScanning ||
        enabled != s_lastEnabled || count != s_lastCount) {
        s_lastScanGen = gen;
        s_lastScanning = scanning;
        s_lastEnabled = enabled;
        s_lastCount = count;

        updateHeaderAndControls();
        rebuildDeviceList();
    }
}

REGISTER_APP_THEMED(bluetooth, "bluetooth", "Bluetooth", LV_SYMBOL_BLUETOOTH, 0xC5B4E3, 0x362B4E, buildAppBluetooth, tickAppBluetooth, refreshAppBluetoothTheme, 50);
