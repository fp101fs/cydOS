#include "ble_hid_mgr.h"
#include "ble_mgr.h"
#include "wifi_mgr.h"
#include "../theme/m3_theme.h"

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <string.h>

static constexpr const char* HID_DEVICE_NAME = "Pixel CYD Remote";
static constexpr uint8_t REPORT_ID_MOUSE     = 1;
static constexpr uint8_t REPORT_ID_CONSUMER  = 2;
static constexpr uint8_t REPORT_ID_KEYBOARD  = 3;

// Composite HID Report Descriptor:
//   Report ID 1: 3-button Mouse + relative X/Y + vertical Wheel (4 bytes)
//   Report ID 2: Consumer Control 16-bit usage (2 bytes) for Media / Fire TV
//   Report ID 3: Standard Keyboard (8 bytes) for D-pad / Enter / Back / Menu
static const uint8_t s_hidReportMap[] = {
    // ---- Report ID 1: Mouse ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_MOUSE,
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5) padding
    0x81, 0x03,        //     Input (Const, Var, Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    // ---- Report ID 2: Consumer Control (16-bit usage) ----
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_CONSUMER,
    0x19, 0x00,        //   Usage Minimum (0x0000)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Abs)
    0xC0,              // End Collection

    // ---- Report ID 3: Keyboard (D-pad / Enter / Esc / Menu) ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_KEYBOARD,
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Var, Abs) - Modifiers
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const) - Reserved
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x73,        //   Logical Maximum (115)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x73,        //   Usage Maximum (115)
    0x81, 0x00,        //   Input (Data, Array, Abs) - Keys
    0xC0               // End Collection
};

static NimBLEServer*         s_server        = nullptr;
static NimBLEHIDDevice*      s_hid           = nullptr;
static NimBLECharacteristic* s_inMouse       = nullptr;
static NimBLECharacteristic* s_inConsumer    = nullptr;
static NimBLECharacteristic* s_inKeyboard    = nullptr;

static bool     s_active        = false;
static bool     s_connected     = false;
static bool     s_pausedWifi    = false;
static uint8_t  s_buttonsMask   = 0;

class HidServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        (void)pServer;
        s_connected = true;
        Serial.println("[HID] Host connected");
    }

    void onDisconnect(NimBLEServer* pServer) override {
        (void)pServer;
        s_connected = false;
        s_buttonsMask = 0;
        Serial.println("[HID] Host disconnected");
        if (s_active) {
            NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
            if (adv) adv->start();
        }
    }
};

static HidServerCallbacks s_serverCbs;

bool bleHidStart() {
    if (s_active) return true;
    if (!g_sys.btEnabled || g_sys.airplaneMode) return false;

    // Stop any active BLE scan to avoid radio contention while advertising HID
    if (bleMgrIsScanning()) {
        bleMgrStopScan();
    }

    // On no-PSRAM ESP32, Wi-Fi STA + NimBLE Peripheral together can exceed internal DRAM.
    // If NimBLE isn't initialized yet and free heap is tight, temporarily pause Wi-Fi so
    // the BLE controller has plenty of RAM (automatically restored when HID stops).
    if (!NimBLEDevice::getInitialized()) {
        uint32_t freeBefore = ESP.getFreeHeap();
        Serial.printf("[HID] Pre-init Free Heap: %lu B (max block: %lu B)\n",
                      (unsigned long)freeBefore, (unsigned long)ESP.getMaxAllocHeap());

        if (freeBefore < 78000 && WiFi.getMode() != WIFI_OFF) {
            Serial.println("[HID] Pausing Wi-Fi temporarily to free DRAM for BLE HID");
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
            g_sys.wifiConnected = false;
            g_sys.wifiConnecting = false;
            s_pausedWifi = true;
            delay(80);
            Serial.printf("[HID] Heap after Wi-Fi pause: %lu B (max block: %lu B)\n",
                          (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
        }

        if (ESP.getFreeHeap() < 48000) {
            Serial.println("[HID] Insufficient heap for NimBLE init");
            return false;
        }

        NimBLEDevice::init(HID_DEVICE_NAME);
    } else {
        NimBLEDevice::setDeviceName(HID_DEVICE_NAME);
    }

    if (!NimBLEDevice::getInitialized()) {
        Serial.println("[HID] NimBLE init failed");
        return false;
    }

    // Just-Works bonding so Android, Fire TV, macOS, Windows, Linux pair cleanly
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    if (!s_server) {
        s_server = NimBLEDevice::createServer();
        if (!s_server) return false;
        s_server->setCallbacks(&s_serverCbs);
    }

    if (!s_hid) {
        s_hid = new NimBLEHIDDevice(s_server);
        s_inMouse    = s_hid->inputReport(REPORT_ID_MOUSE);
        s_inConsumer = s_hid->inputReport(REPORT_ID_CONSUMER);
        s_inKeyboard = s_hid->inputReport(REPORT_ID_KEYBOARD);

        s_hid->manufacturer("Pixel CYD");
        s_hid->pnp(0x02, 0xE502, 0xA111, 0x0110);
        s_hid->hidInfo(0x00, 0x02);
        s_hid->reportMap((uint8_t*)s_hidReportMap, sizeof(s_hidReportMap));
        s_hid->setBatteryLevel(g_sys.batteryPct > 0 ? g_sys.batteryPct : 100);
        s_hid->startServices();
    }

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return false;
    adv->setAppearance(HID_MOUSE);
    adv->addServiceUUID(s_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->start();

    s_active = true;
    s_connected = (s_server->getConnectedCount() > 0);
    Serial.printf("[HID] Advertising '%s' (Free Heap: %lu B)\n",
                  HID_DEVICE_NAME, (unsigned long)ESP.getFreeHeap());
    return true;
}

void bleHidStop() {
    if (!s_active) return;
    s_active = false;
    s_buttonsMask = 0;

    if (NimBLEDevice::getInitialized()) {
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        if (adv && adv->isAdvertising()) {
            adv->stop();
        }
    }
    Serial.println("[HID] Advertising stopped");

    if (s_pausedWifi && g_sys.wifiEnabled && !g_sys.airplaneMode) {
        s_pausedWifi = false;
        Serial.println("[HID] Restoring Wi-Fi");
        wifiMgrSetEnabled(true);
    }
}

bool bleHidIsActive() {
    return s_active;
}

bool bleHidIsConnected() {
    if (!s_active || !s_server) return false;
    s_connected = (s_server->getConnectedCount() > 0);
    return s_connected;
}

const char* bleHidStatusText() {
    if (!g_sys.btEnabled || g_sys.airplaneMode) return "BT Off";
    if (!s_active) return "Start";
    if (bleHidIsConnected()) return "Linked";
    return "Pairing";
}

void bleHidMouseMove(int8_t dx, int8_t dy, int8_t wheel) {
    if (!bleHidIsConnected() || !s_inMouse) return;
    uint8_t report[4] = {
        s_buttonsMask,
        (uint8_t)dx,
        (uint8_t)dy,
        (uint8_t)wheel
    };
    s_inMouse->setValue(report, sizeof(report));
    s_inMouse->notify();
}

void bleHidMouseSetButtons(uint8_t buttonsMask) {
    s_buttonsMask = (uint8_t)(buttonsMask & 0x07);
    if (!bleHidIsConnected() || !s_inMouse) return;
    uint8_t report[4] = { s_buttonsMask, 0, 0, 0 };
    s_inMouse->setValue(report, sizeof(report));
    s_inMouse->notify();
}

void bleHidMouseClick(uint8_t buttonMask) {
    if (!bleHidIsConnected() || !s_inMouse) return;
    uint8_t down[4] = { (uint8_t)(buttonMask & 0x07), 0, 0, 0 };
    uint8_t up[4]   = { 0, 0, 0, 0 };
    s_inMouse->setValue(down, sizeof(down));
    s_inMouse->notify();
    delay(14);
    s_inMouse->setValue(up, sizeof(up));
    s_inMouse->notify();
    s_buttonsMask = 0;
}

void bleHidConsumerTap(uint16_t usageId) {
    if (!bleHidIsConnected() || !s_inConsumer) return;
    uint8_t down[2] = { (uint8_t)(usageId & 0xFF), (uint8_t)((usageId >> 8) & 0xFF) };
    uint8_t up[2]   = { 0, 0 };
    s_inConsumer->setValue(down, sizeof(down));
    s_inConsumer->notify();
    delay(18);
    s_inConsumer->setValue(up, sizeof(up));
    s_inConsumer->notify();
}

void bleHidKeyTap(uint8_t hidKey, uint8_t modifiers) {
    if (!bleHidIsConnected() || !s_inKeyboard) return;
    uint8_t down[8] = { modifiers, 0, hidKey, 0, 0, 0, 0, 0 };
    uint8_t up[8]   = { 0, 0, 0, 0, 0, 0, 0, 0 };
    s_inKeyboard->setValue(down, sizeof(down));
    s_inKeyboard->notify();
    delay(18);
    s_inKeyboard->setValue(up, sizeof(up));
    s_inKeyboard->notify();
}
