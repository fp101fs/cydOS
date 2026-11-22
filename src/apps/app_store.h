#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

enum class AppStoreTab : uint8_t {
    Explore = 0,
    SdCard = 1,
    Installed = 2,
    Tools = 3,
    Count = 4
};

struct AppStoreStorageSummary {
    uint32_t dynamicTotalBytes = 0;
    uint32_t usedBytes = 0;
    uint32_t freeBytes = 0;
    uint8_t usedPct = 0;
    uint8_t installedCount = 0;
};

// Pure helpers (host-testable without ESP32/LVGL)
const char* appStoreTabLabel(AppStoreTab tab);
AppStoreTab appStoreTabFromIndex(int index);

AppStoreStorageSummary appStoreComputeStorageSummary(uint32_t dynamicTotalBytes,
                                                     uint32_t freeBytes,
                                                     uint8_t installedCount);

void appStoreFormatSizeCompact(uint32_t bytes, char* out, size_t outSize);

// Formats a concise metadata subtitle for an installed App card, e.g.:
//   "1.2 MB • 1 data • Boot"
//   "896 KB • Backup"
//   "512 KB"
void appStoreFormatInstalledMeta(uint32_t appSizeBytes,
                                 uint8_t dataPartitionCount,
                                 bool isDefaultBoot,
                                 bool hasBackup,
                                 char* out,
                                 size_t outSize);

#ifndef APP_STORE_HOST_TEST

// Open the App Store directly on a specific tab (or switch tabs if already open).
void appStoreOpenTab(AppStoreTab tab);

// Re-render the active tab if the App Store is currently open.
void appStoreRefreshCurrentTab();

// Safe async notification called when installed apps change (install/rename/uninstall).
void appStoreNotifyAppsChanged();

// Returns true while the App Store UI body is mounted.
bool appStoreIsOpen();

#endif // !APP_STORE_HOST_TEST
