#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr size_t WIFI_MAX_SCAN_RESULTS = 16;
static constexpr size_t WIFI_SSID_MAX_LEN     = 32;
static constexpr size_t WIFI_PASS_MAX_LEN     = 64;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

struct WifiScanEntry {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t authMode;
    bool isOpen;
};

enum class WifiConnEvent : uint8_t {
    NONE = 0,
    CONNECTED,
    DISCONNECTED,
    FAILED
};

void wifiMgrInit();
bool wifiMgrTick();

void wifiMgrSetEnabled(bool enabled);
void wifiMgrSetAirplaneMode(bool airplane);

bool wifiMgrStartScan();
bool wifiMgrIsScanning();
uint32_t wifiMgrGetScanGen();
size_t wifiMgrGetScanCount();
const WifiScanEntry* wifiMgrGetScanResult(size_t index);

void wifiMgrConnect(const char* ssid, const char* password, bool saveOnConnect = true);
void wifiMgrDisconnect();

bool wifiMgrHasSavedCreds();
const char* wifiMgrGetSavedSsid();
const char* wifiMgrGetSavedPass();
void wifiMgrForgetSaved();

WifiConnEvent wifiMgrConsumeEvent();

int8_t wifiMgrRssiToBars(int8_t rssi);
const char* wifiMgrAuthModeName(uint8_t authMode);
const char* wifiMgrStatusText();
