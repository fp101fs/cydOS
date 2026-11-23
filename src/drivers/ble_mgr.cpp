#include "ble_mgr.h"
#include "../theme/m3_theme.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

static constexpr const char* BLE_NVS_NS = "bt";
static constexpr const char* BLE_DEVICE_NAME = "Pixel 8 CYD";
static constexpr uint32_t    BLE_SCAN_TIMEOUT_PAD_MS = 3500;

#ifdef ESP32
static portMUX_TYPE s_bleMux = portMUX_INITIALIZER_UNLOCKED;
#define BLE_LOCK()   portENTER_CRITICAL(&s_bleMux)
#define BLE_UNLOCK() portEXIT_CRITICAL(&s_bleMux)
#else
#define BLE_LOCK()   do {} while (0)
#define BLE_UNLOCK() do {} while (0)
#endif

static BleScanEntry* s_scanList = nullptr;
static size_t        s_scanCount = 0;
static uint32_t      s_scanGen = 0;

static BleScanEntry* s_pendingList = nullptr;
static size_t        s_pendingCount = 0;
static volatile bool s_pendingReady = false;

static bool ensureBleScanBufs() {
    if (!s_scanList) {
        s_scanList = (BleScanEntry*)calloc(BLE_MAX_SCAN_RESULTS, sizeof(BleScanEntry));
    }
    if (!s_pendingList) {
        s_pendingList = (BleScanEntry*)calloc(BLE_MAX_SCAN_RESULTS, sizeof(BleScanEntry));
    }
    return s_scanList && s_pendingList;
}

static bool         s_initialized = false;
static bool         s_scanning = false;
static uint32_t     s_scanStartMs = 0;
static uint32_t     s_scanTimeoutMs = 0;
static BleEvent     s_lastEvent = BleEvent::NONE;
static char         s_statusBuf[32] = "";

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
    if (prefs.begin(BLE_NVS_NS, false)) {
        prefs.putBool("en", enabled);
        prefs.end();
    }
}

void bleMgrFormatMacUpper(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < dstSize; ++i) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i] = c;
    }
    dst[i] = '\0';
}

size_t bleMgrMergeAndSortResults(
    const BleScanEntry* raw,
    size_t rawCount,
    BleScanEntry* out,
    size_t outCapacity
) {
    if (!raw || !out || outCapacity == 0) return 0;

    BleScanEntry dedup[BLE_MAX_SCAN_RESULTS];
    size_t limit = outCapacity < BLE_MAX_SCAN_RESULTS ? outCapacity : BLE_MAX_SCAN_RESULTS;
    size_t count = 0;

    for (size_t i = 0; i < rawCount; ++i) {
        if (raw[i].addr[0] == '\0') continue;

        char mac[BLE_ADDR_STR_LEN];
        bleMgrFormatMacUpper(mac, sizeof(mac), raw[i].addr);

        int existing = -1;
        for (size_t j = 0; j < count; ++j) {
            if (strcmp(dedup[j].addr, mac) == 0) {
                existing = (int)j;
                break;
            }
        }

        if (existing >= 0) {
            if (raw[i].rssi > dedup[existing].rssi) {
                dedup[existing].rssi = raw[i].rssi;
            }
            if (!dedup[existing].hasName && raw[i].hasName && raw[i].name[0] != '\0') {
                copyStr(dedup[existing].name, sizeof(dedup[existing].name), raw[i].name);
                dedup[existing].hasName = true;
            }
            if (raw[i].connectable) {
                dedup[existing].connectable = true;
            }
        } else {
            BleScanEntry item;
            memset(&item, 0, sizeof(item));
            copyStr(item.name, sizeof(item.name), raw[i].hasName ? raw[i].name : "");
            copyStr(item.addr, sizeof(item.addr), mac);
            item.rssi = raw[i].rssi;
            item.connectable = raw[i].connectable;
            item.hasName = raw[i].hasName && item.name[0] != '\0';

            if (count < limit) {
                dedup[count++] = item;
            } else {
                // Replace the weakest entry if the new device has a stronger RSSI
                size_t worstIdx = 0;
                for (size_t j = 1; j < count; ++j) {
                    if (dedup[j].rssi < dedup[worstIdx].rssi) {
                        worstIdx = j;
                    }
                }
                if (item.rssi > dedup[worstIdx].rssi) {
                    dedup[worstIdx] = item;
                }
            }
        }
    }

    // Sort descending by RSSI; tie-break named devices first, then MAC address
    for (size_t i = 1; i < count; ++i) {
        BleScanEntry key = dedup[i];
        size_t j = i;
        while (j > 0) {
            const BleScanEntry& prev = dedup[j - 1];
            bool keyBetter = false;
            if (key.rssi != prev.rssi) {
                keyBetter = key.rssi > prev.rssi;
            } else if (key.hasName != prev.hasName) {
                keyBetter = key.hasName;
            } else {
                keyBetter = strcmp(key.addr, prev.addr) < 0;
            }
            if (!keyBetter) break;
            dedup[j] = dedup[j - 1];
            --j;
        }
        dedup[j] = key;
    }

    for (size_t i = 0; i < count; ++i) {
        out[i] = dedup[i];
    }
    return count;
}

static void bleScanCompleteCb(NimBLEScanResults results) {
    int n = results.getCount();
    if (n < 0) n = 0;

    // Keep NimBLE host-task stack usage tiny and never allocate inside critical section:
    // s_pendingList is only read by bleMgrTick() once s_pendingReady == true.
    if (!s_pendingList || s_pendingReady) return;

    size_t rawCount = 0;
    for (int i = 0; i < n && rawCount < BLE_MAX_SCAN_RESULTS; ++i) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        BleScanEntry& entry = s_pendingList[rawCount];
        memset(&entry, 0, sizeof(entry));

        std::string addrStr = dev.getAddress().toString();
        bleMgrFormatMacUpper(entry.addr, sizeof(entry.addr), addrStr.c_str());

        if (dev.haveName()) {
            std::string nameStr = dev.getName();
            if (!nameStr.empty()) {
                copyStr(entry.name, sizeof(entry.name), nameStr.c_str());
                entry.hasName = true;
            }
        }

        int r = dev.getRSSI();
        if (r < -127) r = -127;
        if (r > 0) r = 0;
        entry.rssi = (int8_t)r;
        entry.connectable = dev.isConnectable();
        rawCount++;
    }

    BLE_LOCK();
    s_pendingCount = rawCount;
    s_pendingReady = true;
    BLE_UNLOCK();
}

static bool ensureNimBleInit() {
    if (!ensureBleScanBufs()) return false;
    if (NimBLEDevice::getInitialized()) {
        s_initialized = true;
        return true;
    }
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t maxBlock = ESP.getMaxAllocHeap();
    if (freeHeap < 48000 || maxBlock < 20000) {
        Serial.printf("[BLE] Low heap for NimBLE init (free=%lu maxBlk=%lu)\n",
                      (unsigned long)freeHeap, (unsigned long)maxBlock);
        return false;
    }
    NimBLEDevice::init(BLE_DEVICE_NAME);
    s_initialized = NimBLEDevice::getInitialized();
    if (!s_initialized) {
        Serial.println("[BLE] NimBLE init failed");
        return false;
    }
    Serial.printf("[BLE] NimBLE initialized (Free Heap: %lu B)\n",
                  (unsigned long)ESP.getFreeHeap());
    return true;
}

static void shutdownNimBle() {
    if (s_initialized || NimBLEDevice::getInitialized()) {
        NimBLEScan* pScan = NimBLEDevice::getScan();
        if (pScan) {
            if (pScan->isScanning()) {
                pScan->stop();
            }
            pScan->clearResults();
        }
        // Keep BT controller memory available so Bluetooth can be re-enabled without reboot
        NimBLEDevice::deinit(false);
        s_initialized = false;
    }
    BLE_LOCK();
    s_pendingReady = false;
    s_pendingCount = 0;
    BLE_UNLOCK();
    s_scanning = false;
    g_sys.btScanning = false;
}

int8_t bleMgrRssiToBars(int8_t rssi) {
    if (rssi >= 0) return 0;
    if (rssi >= -60) return 4;
    if (rssi >= -72) return 3;
    if (rssi >= -84) return 2;
    return 1;
}

const char* bleMgrStatusText() {
    if (!g_sys.btEnabled || g_sys.airplaneMode) return "Off";
    if (s_scanning) return "Scanning...";
    if (g_sys.btDevice[0] != '\0') return g_sys.btDevice;
    if (s_scanCount > 0) {
        snprintf(s_statusBuf, sizeof(s_statusBuf), "%u nearby", (unsigned)s_scanCount);
        return s_statusBuf;
    }
    return "Ready";
}

void bleMgrInit() {
    s_scanCount = 0;
    s_scanGen = 0;
    s_pendingCount = 0;
    s_pendingReady = false;
    s_initialized = false;
    s_scanning = false;
    s_scanStartMs = 0;
    s_scanTimeoutMs = 0;
    s_lastEvent = BleEvent::NONE;
    g_sys.btScanning = false;
    g_sys.btFoundCount = 0;
    g_sys.btDevice[0] = '\0';

    bool enabled = true;
    Preferences prefs;
    if (prefs.begin(BLE_NVS_NS, true)) {
        enabled = prefs.getBool("en", true);
        prefs.end();
    }
    g_sys.btEnabled = enabled;
    // NimBLE is initialized lazily on first scan to keep boot heap high for Wi-Fi/TLS
}

void bleMgrSetEnabled(bool enabled) {
    g_sys.btEnabled = enabled;
    if (enabled && g_sys.airplaneMode) {
        g_sys.airplaneMode = false;
    }
    saveEnabledNvs(enabled);

    if (!enabled) {
        shutdownNimBle();
        s_scanCount = 0;
        s_scanGen++;
        g_sys.btFoundCount = 0;
        g_sys.btDevice[0] = '\0';
    }
}

void bleMgrSetAirplaneMode(bool airplane) {
    if (airplane) {
        shutdownNimBle();
    }
}

bool bleMgrStartScan(uint32_t durationSec) {
    if (!g_sys.btEnabled || g_sys.airplaneMode) {
        return false;
    }
    if (s_scanning) {
        return true;
    }
    if (durationSec == 0) {
        durationSec = BLE_SCAN_DURATION_SEC;
    }

    if (!ensureNimBleInit()) {
        s_lastEvent = BleEvent::SCAN_FAILED;
        return false;
    }

    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (!pScan) {
        s_lastEvent = BleEvent::SCAN_FAILED;
        return false;
    }

    pScan->clearResults();
    pScan->setActiveScan(true);
    // Wi-Fi coexistence friendly scan duty cycle (45 ms window / 100 ms interval)
    pScan->setInterval(100);
    pScan->setWindow(45);
    pScan->setMaxResults(BLE_MAX_SCAN_RESULTS);

    BLE_LOCK();
    s_pendingReady = false;
    s_pendingCount = 0;
    BLE_UNLOCK();

    bool ok = pScan->start(durationSec, bleScanCompleteCb, false);
    if (!ok) {
        Serial.println("[BLE] Scan start failed");
        s_scanning = false;
        g_sys.btScanning = false;
        s_lastEvent = BleEvent::SCAN_FAILED;
        return false;
    }

    s_scanning = true;
    g_sys.btScanning = true;
    s_scanStartMs = millis();
    s_scanTimeoutMs = durationSec * 1000UL + BLE_SCAN_TIMEOUT_PAD_MS;
    Serial.printf("[BLE] Async scan started (%lus)\n", (unsigned long)durationSec);
    return true;
}

void bleMgrStopScan() {
    if (!s_scanning) return;

    if (s_initialized && NimBLEDevice::getInitialized()) {
        NimBLEScan* pScan = NimBLEDevice::getScan();
        if (pScan && pScan->isScanning()) {
            pScan->stop();
        }
    }

    BLE_LOCK();
    s_pendingReady = false;
    s_pendingCount = 0;
    BLE_UNLOCK();

    s_scanning = false;
    g_sys.btScanning = false;
    s_scanGen++;
}

void bleMgrClearResults() {
    s_scanCount = 0;
    g_sys.btFoundCount = 0;
    g_sys.btDevice[0] = '\0';
    s_scanGen++;
}

bool bleMgrTick() {
    bool changed = false;

    bool ready = false;
    BleScanEntry staging[BLE_MAX_SCAN_RESULTS];
    size_t stagingCount = 0;

    BLE_LOCK();
    if (s_pendingReady && s_pendingList) {
        ready = true;
        stagingCount = s_pendingCount;
        for (size_t i = 0; i < stagingCount; ++i) {
            staging[i] = s_pendingList[i];
        }
        s_pendingReady = false;
    }
    BLE_UNLOCK();

    if (ready && s_scanList) {
        s_scanCount = bleMgrMergeAndSortResults(staging, stagingCount, s_scanList, BLE_MAX_SCAN_RESULTS);
        s_scanning = false;
        g_sys.btScanning = false;
        g_sys.btFoundCount = (uint8_t)s_scanCount;
        s_scanGen++;
        s_lastEvent = BleEvent::SCAN_DONE;
        changed = true;

        if (s_initialized && NimBLEDevice::getInitialized()) {
            NimBLEScan* pScan = NimBLEDevice::getScan();
            if (pScan) pScan->clearResults();
        }
        Serial.printf("[BLE] Scan complete: %u device(s)\n", (unsigned)s_scanCount);
    } else if (s_scanning && (millis() - s_scanStartMs >= s_scanTimeoutMs)) {
        // Safety guard if NimBLE callback never fired
        Serial.println("[BLE] Scan timeout");
        bleMgrStopScan();
        s_lastEvent = BleEvent::SCAN_FAILED;
        changed = true;
    }

    return changed;
}

bool bleMgrIsScanning() {
    return s_scanning;
}

bool bleMgrIsInitialized() {
    return s_initialized;
}

uint32_t bleMgrGetScanGen() {
    return s_scanGen;
}

size_t bleMgrGetScanCount() {
    return s_scanCount;
}

const BleScanEntry* bleMgrGetScanResult(size_t index) {
    if (!s_scanList || index >= s_scanCount) return nullptr;
    return &s_scanList[index];
}

BleEvent bleMgrConsumeEvent() {
    BleEvent ev = s_lastEvent;
    s_lastEvent = BleEvent::NONE;
    return ev;
}
