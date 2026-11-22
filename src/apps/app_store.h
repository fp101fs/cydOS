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

enum class AppStoreSort : uint8_t {
    Popular = 0,
    Recent = 1,
    Name = 2,
    Starred = 3,
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

const char* appStoreSortLabel(AppStoreSort sort);
const char* appStoreSortApiOrder(AppStoreSort sort);
bool appStoreSortStarOnly(AppStoreSort sort);
AppStoreSort appStoreSortFromIndex(int index);

void appStoreFormatPageLabel(uint8_t page, int totalPages, int totalItems, char* out, size_t outSize);
void appStoreFormatVersionMeta(uint32_t appSizeBytes,
                               uint32_t fileSizeBytes,
                               const char* publishedAt,
                               bool hasData,
                               char* out,
                               size_t outSize);
bool appStoreIsValidInstallUrl(const char* url);

// Pure SD Card browser helpers (host-testable)
bool appStoreIsBinFile(const char* name);
std::string appStoreNormalizeSdPath(const char* path);
std::string appStoreParentPath(const char* path);
void appStoreFormatSdPathDisplay(const char* path, char* out, size_t outSize);
bool appStoreIsValidSdName(const char* name);

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
