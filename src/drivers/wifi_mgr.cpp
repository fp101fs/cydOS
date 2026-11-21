#include "wifi_mgr.h"
#include "../theme/m3_theme.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

static constexpr const char* WIFI_NVS_NS = "wifi";
static constexpr uint32_t WIFI_POLL_INTERVAL_MS = 250;

static WifiScanEntry s_scanList[WIFI_MAX_SCAN_RESULTS];
static size_t s_scanCount = 0;
static bool s_scanning = false;
static uint32_t s_scanGen = 0;

static char s_savedSsid[WIFI_SSID_MAX_LEN + 1] = "";
static char s_savedPass[WIFI_PASS_MAX_LEN + 1] = "";

static char s_pendingSsid[WIFI_SSID_MAX_LEN + 1] = "";
static char s_pendingPass[WIFI_PASS_MAX_LEN + 1] = "";
static bool s_savePending = false;

static uint32_t s_connectStartMs = 0;
static uint32_t s_lastPollMs = 0;
static WifiConnEvent s_lastEvent = WifiConnEvent::NONE;

static void copyStr(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void saveEnabledNvs(bool enabled) {
    Preferences prefs;
    if (prefs.begin(WIFI_NVS_NS, false)) {
        prefs.putBool("en", enabled);
        prefs.end();
    }
}

static void saveCredsNvs(const char* ssid, const char* pass) {
    copyStr(s_savedSsid, sizeof(s_savedSsid), ssid);
    copyStr(s_savedPass, sizeof(s_savedPass), pass);
    Preferences prefs;
    if (prefs.begin(WIFI_NVS_NS, false)) {
        prefs.putString("ssid", s_savedSsid);
        prefs.putString("pass", s_savedPass);
        prefs.end();
    }
}

static void clearLiveConnState() {
    g_sys.wifiConnected = false;
    g_sys.wifiConnecting = false;
    g_sys.wifiIp[0] = '\0';
    g_sys.wifiRssi = 0;
    g_sys.wifiBars = 0;
}

int8_t wifiMgrRssiToBars(int8_t rssi) {
    if (rssi >= 0) return 0;
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    return 1;
}

const char* wifiMgrAuthModeName(uint8_t authMode) {
    switch ((wifi_auth_mode_t)authMode) {
        case WIFI_AUTH_OPEN:            return "Open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "Secured";
    }
}

const char* wifiMgrStatusText() {
    if (!g_sys.wifiEnabled || g_sys.airplaneMode) return "Off";
    if (g_sys.wifiConnected && g_sys.wifiSsid[0]) return g_sys.wifiSsid;
    if (g_sys.wifiConnecting) return "Connecting...";
    return "Not connected";
}

void wifiMgrInit() {
    s_scanCount = 0;
    s_scanning = false;
    s_scanGen = 0;
    s_savedSsid[0] = '\0';
    s_savedPass[0] = '\0';
    s_pendingSsid[0] = '\0';
    s_pendingPass[0] = '\0';
    s_savePending = false;
    s_lastEvent = WifiConnEvent::NONE;
    s_lastPollMs = 0;
    clearLiveConnState();
    g_sys.wifiSsid[0] = '\0';

    bool enabled = true;
    Preferences prefs;
    if (prefs.begin(WIFI_NVS_NS, true)) {
        enabled = prefs.getBool("en", true);
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        copyStr(s_savedSsid, sizeof(s_savedSsid), ssid.c_str());
        copyStr(s_savedPass, sizeof(s_savedPass), pass.c_str());
        prefs.end();
    }

    g_sys.wifiEnabled = enabled;
    WiFi.persistent(false);

    if (g_sys.wifiEnabled && !g_sys.airplaneMode) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        if (s_savedSsid[0] != '\0') {
            Serial.printf("[WIFI] Auto-connecting to saved SSID: %s\n", s_savedSsid);
            wifiMgrConnect(s_savedSsid, s_savedPass, false);
        }
    } else {
        WiFi.mode(WIFI_OFF);
    }
}

void wifiMgrSetEnabled(bool enabled) {
    g_sys.wifiEnabled = enabled;
    if (enabled && g_sys.airplaneMode) {
        g_sys.airplaneMode = false;
    }
    saveEnabledNvs(enabled);

    if (enabled) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        if (s_savedSsid[0] != '\0') {
            wifiMgrConnect(s_savedSsid, s_savedPass, false);
        }
    } else {
        if (s_scanning) {
            WiFi.scanDelete();
            s_scanning = false;
        }
        s_scanCount = 0;
        s_scanGen++;
        s_savePending = false;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        clearLiveConnState();
        g_sys.wifiSsid[0] = '\0';
    }
}

void wifiMgrSetAirplaneMode(bool airplane) {
    g_sys.airplaneMode = airplane;
    if (airplane) {
        if (s_scanning) {
            WiFi.scanDelete();
            s_scanning = false;
        }
        s_savePending = false;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        clearLiveConnState();
    } else if (g_sys.wifiEnabled) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        if (s_savedSsid[0] != '\0') {
            wifiMgrConnect(s_savedSsid, s_savedPass, false);
        }
    }
}

bool wifiMgrStartScan() {
    if (!g_sys.wifiEnabled || g_sys.airplaneMode) return false;
    if (s_scanning) return true;

    if (g_sys.wifiConnecting) {
        WiFi.disconnect(false);
        g_sys.wifiConnecting = false;
        s_savePending = false;
    }

    WiFi.mode(WIFI_STA);
    int16_t res = WiFi.scanNetworks(true, false);
    if (res == WIFI_SCAN_RUNNING || res >= 0) {
        s_scanning = true;
        return true;
    }
    return false;
}

bool wifiMgrIsScanning() {
    return s_scanning;
}

uint32_t wifiMgrGetScanGen() {
    return s_scanGen;
}

size_t wifiMgrGetScanCount() {
    return s_scanCount;
}

const WifiScanEntry* wifiMgrGetScanResult(size_t index) {
    if (index >= s_scanCount) return nullptr;
    return &s_scanList[index];
}

void wifiMgrConnect(const char* ssid, const char* password, bool saveOnConnect) {
    if (!ssid || !ssid[0]) return;
    if (!g_sys.wifiEnabled || g_sys.airplaneMode) return;

    if (s_scanning) {
        WiFi.scanDelete();
        s_scanning = false;
    }

    copyStr(s_pendingSsid, sizeof(s_pendingSsid), ssid);
    copyStr(s_pendingPass, sizeof(s_pendingPass), password ? password : "");
    s_savePending = saveOnConnect;

    copyStr(g_sys.wifiSsid, sizeof(g_sys.wifiSsid), s_pendingSsid);
    g_sys.wifiConnected = false;
    g_sys.wifiConnecting = true;
    g_sys.wifiIp[0] = '\0';
    g_sys.wifiRssi = 0;
    g_sys.wifiBars = 0;
    s_connectStartMs = millis();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (s_pendingPass[0] != '\0') {
        WiFi.begin(s_pendingSsid, s_pendingPass);
    } else {
        WiFi.begin(s_pendingSsid);
    }
}

void wifiMgrDisconnect() {
    s_savePending = false;
    WiFi.disconnect(false);
    clearLiveConnState();
}

bool wifiMgrHasSavedCreds() {
    return s_savedSsid[0] != '\0';
}

const char* wifiMgrGetSavedSsid() {
    return s_savedSsid;
}

const char* wifiMgrGetSavedPass() {
    return s_savedPass;
}

void wifiMgrForgetSaved() {
    bool wasTarget = (s_savedSsid[0] != '\0' && strcmp(g_sys.wifiSsid, s_savedSsid) == 0);
    s_savedSsid[0] = '\0';
    s_savedPass[0] = '\0';

    Preferences prefs;
    if (prefs.begin(WIFI_NVS_NS, false)) {
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.end();
    }

    if (wasTarget && (g_sys.wifiConnected || g_sys.wifiConnecting)) {
        wifiMgrDisconnect();
        g_sys.wifiSsid[0] = '\0';
    }
}

WifiConnEvent wifiMgrConsumeEvent() {
    WifiConnEvent ev = s_lastEvent;
    s_lastEvent = WifiConnEvent::NONE;
    return ev;
}

static void ingestScanResults(int16_t rawCount) {
    s_scanCount = 0;
    for (int16_t i = 0; i < rawCount; ++i) {
        String ssidStr = WiFi.SSID(i);
        if (ssidStr.length() == 0) continue;

        int32_t rawRssi = WiFi.RSSI(i);
        if (rawRssi < -127) rawRssi = -127;
        if (rawRssi > 0) rawRssi = 0;
        int8_t rssi = (int8_t)rawRssi;
        uint8_t auth = (uint8_t)WiFi.encryptionType(i);

        // Deduplicate by SSID, keeping strongest RSSI
        bool found = false;
        for (size_t j = 0; j < s_scanCount; ++j) {
            if (strcmp(s_scanList[j].ssid, ssidStr.c_str()) == 0) {
                if (rssi > s_scanList[j].rssi) {
                    s_scanList[j].rssi = rssi;
                    s_scanList[j].authMode = auth;
                    s_scanList[j].isOpen = (auth == (uint8_t)WIFI_AUTH_OPEN);
                }
                found = true;
                break;
            }
        }
        if (found) continue;

        if (s_scanCount < WIFI_MAX_SCAN_RESULTS) {
            WifiScanEntry& e = s_scanList[s_scanCount++];
            copyStr(e.ssid, sizeof(e.ssid), ssidStr.c_str());
            e.rssi = rssi;
            e.authMode = auth;
            e.isOpen = (auth == (uint8_t)WIFI_AUTH_OPEN);
        } else {
            // Replace weakest entry if this AP is stronger
            size_t minIdx = 0;
            for (size_t j = 1; j < s_scanCount; ++j) {
                if (s_scanList[j].rssi < s_scanList[minIdx].rssi) minIdx = j;
            }
            if (rssi > s_scanList[minIdx].rssi) {
                WifiScanEntry& e = s_scanList[minIdx];
                copyStr(e.ssid, sizeof(e.ssid), ssidStr.c_str());
                e.rssi = rssi;
                e.authMode = auth;
                e.isOpen = (auth == (uint8_t)WIFI_AUTH_OPEN);
            }
        }
    }

    // Insertion sort descending by RSSI
    for (size_t i = 1; i < s_scanCount; ++i) {
        WifiScanEntry key = s_scanList[i];
        size_t j = i;
        while (j > 0 && s_scanList[j - 1].rssi < key.rssi) {
            s_scanList[j] = s_scanList[j - 1];
            --j;
        }
        s_scanList[j] = key;
    }

    WiFi.scanDelete();
    s_scanning = false;
    s_scanGen++;
}

bool wifiMgrTick() {
    bool changed = false;

    if (s_scanning) {
        int16_t n = WiFi.scanComplete();
        if (n >= 0) {
            ingestScanResults(n);
            changed = true;
        } else if (n == WIFI_SCAN_FAILED) {
            s_scanning = false;
            s_scanGen++;
            changed = true;
        }
    }

    uint32_t now = millis();
    if (s_lastPollMs != 0 && (uint32_t)(now - s_lastPollMs) < WIFI_POLL_INTERVAL_MS) {
        return changed;
    }
    s_lastPollMs = now;

    if (!g_sys.wifiEnabled || g_sys.airplaneMode) {
        if (g_sys.wifiConnected || g_sys.wifiConnecting || g_sys.wifiBars != 0) {
            clearLiveConnState();
            changed = true;
        }
        return changed;
    }

    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        bool wasConnected = g_sys.wifiConnected;
        int8_t prevBars = g_sys.wifiBars;

        g_sys.wifiConnected = true;
        g_sys.wifiConnecting = false;

        String ssid = WiFi.SSID();
        if (ssid.length() > 0) {
            copyStr(g_sys.wifiSsid, sizeof(g_sys.wifiSsid), ssid.c_str());
        }
        String ipStr = WiFi.localIP().toString();
        if (strcmp(g_sys.wifiIp, ipStr.c_str()) != 0) {
            copyStr(g_sys.wifiIp, sizeof(g_sys.wifiIp), ipStr.c_str());
            changed = true;
        }

        int32_t r = WiFi.RSSI();
        if (r < -127) r = -127;
        if (r > 0) r = 0;
        g_sys.wifiRssi = (int8_t)r;
        g_sys.wifiBars = wifiMgrRssiToBars(g_sys.wifiRssi);
        if (g_sys.wifiBars != prevBars) changed = true;

        if (!wasConnected) {
            if (s_savePending && s_pendingSsid[0] != '\0') {
                saveCredsNvs(s_pendingSsid, s_pendingPass);
                s_savePending = false;
            }
            s_lastEvent = WifiConnEvent::CONNECTED;
            changed = true;
            Serial.printf("[WIFI] Connected: %s IP=%s RSSI=%d\n",
                          g_sys.wifiSsid, g_sys.wifiIp, (int)g_sys.wifiRssi);
        }
    } else {
        if (g_sys.wifiConnected) {
            clearLiveConnState();
            s_lastEvent = WifiConnEvent::DISCONNECTED;
            changed = true;
            Serial.println("[WIFI] Disconnected");
        } else if (g_sys.wifiConnecting) {
            bool hardFail = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
            bool timedOut = ((uint32_t)(now - s_connectStartMs) >= WIFI_CONNECT_TIMEOUT_MS);
            if (hardFail || timedOut) {
                WiFi.disconnect(false);
                g_sys.wifiConnecting = false;
                s_savePending = false;
                s_lastEvent = WifiConnEvent::FAILED;
                changed = true;
                Serial.printf("[WIFI] Connect failed (status=%d, timeout=%d)\n",
                              (int)st, timedOut ? 1 : 0);
            }
        }
    }

    return changed;
}
