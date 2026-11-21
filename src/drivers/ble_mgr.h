#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr size_t   BLE_MAX_SCAN_RESULTS  = 20;
static constexpr size_t   BLE_NAME_MAX_LEN      = 32;
static constexpr size_t   BLE_ADDR_STR_LEN      = 18; // "AA:BB:CC:DD:EE:FF\0"
static constexpr uint32_t BLE_SCAN_DURATION_SEC = 5;

struct BleScanEntry {
    char   name[BLE_NAME_MAX_LEN + 1];
    char   addr[BLE_ADDR_STR_LEN];
    int8_t rssi;
    bool   connectable;
    bool   hasName;
};

enum class BleEvent : uint8_t {
    NONE = 0,
    SCAN_DONE,
    SCAN_FAILED
};

void bleMgrInit();
bool bleMgrTick();

void bleMgrSetEnabled(bool enabled);
void bleMgrSetAirplaneMode(bool airplane);

bool bleMgrStartScan(uint32_t durationSec = BLE_SCAN_DURATION_SEC);
void bleMgrStopScan();
void bleMgrClearResults();

bool bleMgrIsScanning();
bool bleMgrIsInitialized();

uint32_t bleMgrGetScanGen();
size_t bleMgrGetScanCount();
const BleScanEntry* bleMgrGetScanResult(size_t index);

BleEvent bleMgrConsumeEvent();

int8_t bleMgrRssiToBars(int8_t rssi);
const char* bleMgrStatusText();

// Pure helpers exposed for deterministic unit testing
void bleMgrFormatMacUpper(char* dst, size_t dstSize, const char* src);
size_t bleMgrMergeAndSortResults(
    const BleScanEntry* raw,
    size_t rawCount,
    BleScanEntry* out,
    size_t outCapacity
);
