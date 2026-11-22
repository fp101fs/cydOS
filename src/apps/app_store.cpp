#include "app_store.h"

#include <cstdio>
#include <cstring>

const char* appStoreTabLabel(AppStoreTab tab) {
    switch (tab) {
        case AppStoreTab::Explore:   return "Explore";
        case AppStoreTab::SdCard:    return "SD Card";
        case AppStoreTab::Installed: return "Installed";
        case AppStoreTab::Tools:     return "Tools";
        default:                     return "Installed";
    }
}

AppStoreTab appStoreTabFromIndex(int index) {
    if (index <= 0) return AppStoreTab::Explore;
    if (index == 1) return AppStoreTab::SdCard;
    if (index == 2) return AppStoreTab::Installed;
    return AppStoreTab::Tools;
}

const char* appStoreSortLabel(AppStoreSort sort) {
    switch (sort) {
        case AppStoreSort::Popular: return "Popular";
        case AppStoreSort::Recent:  return "Recent";
        case AppStoreSort::Name:    return "A-Z";
        case AppStoreSort::Starred: return "Starred";
        default:                    return "Popular";
    }
}

const char* appStoreSortApiOrder(AppStoreSort sort) {
    switch (sort) {
        case AppStoreSort::Popular: return "downloads";
        case AppStoreSort::Recent:  return "published_at";
        case AppStoreSort::Name:    return "name";
        case AppStoreSort::Starred: return "downloads";
        default:                    return "downloads";
    }
}

bool appStoreSortStarOnly(AppStoreSort sort) {
    return sort == AppStoreSort::Starred;
}

AppStoreSort appStoreSortFromIndex(int index) {
    if (index <= 0) return AppStoreSort::Popular;
    if (index == 1) return AppStoreSort::Recent;
    if (index == 2) return AppStoreSort::Name;
    return AppStoreSort::Starred;
}

void appStoreFormatPageLabel(uint8_t page, int totalPages, int totalItems, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    int safePages = totalPages < 1 ? 1 : totalPages;
    int safePage = page < 1 ? 1 : page;
    if (safePage > safePages) safePage = safePages;
    if (totalItems > 0) {
        snprintf(out, outSize, "Page %d/%d (%d)", safePage, safePages, totalItems);
    } else {
        snprintf(out, outSize, "Page %d/%d", safePage, safePages);
    }
}

void appStoreFormatVersionMeta(uint32_t appSizeBytes,
                               uint32_t fileSizeBytes,
                               const char* publishedAt,
                               bool hasData,
                               char* out,
                               size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    size_t pos = 0;

    uint32_t shownBytes = appSizeBytes > 0 ? appSizeBytes : fileSizeBytes;
    if (shownBytes > 0) {
        char sz[24];
        appStoreFormatSizeCompact(shownBytes, sz, sizeof(sz));
        int w = snprintf(out, outSize, "%s", sz);
        if (w > 0) pos = static_cast<size_t>(w) < outSize ? static_cast<size_t>(w) : outSize - 1;
    }

    if (publishedAt && publishedAt[0] && pos < outSize) {
        int w = snprintf(out + pos, outSize - pos, "%s%.10s", pos > 0 ? " • " : "", publishedAt);
        if (w > 0) pos += static_cast<size_t>(w);
        if (pos >= outSize) pos = outSize - 1;
    }

    if (hasData && pos < outSize) {
        snprintf(out + pos, outSize - pos, "%s+Data", pos > 0 ? " • " : "");
    }

    if (out[0] == '\0') {
        snprintf(out, outSize, "Ready to install");
    }
}

bool appStoreIsValidInstallUrl(const char* url) {
    if (!url) return false;
    while (*url == ' ' || *url == '\t' || *url == '\r' || *url == '\n') ++url;
    size_t prefixLen = 0;
    if (strncmp(url, "https://", 8) == 0) {
        prefixLen = 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        prefixLen = 7;
    } else {
        return false;
    }
    const char* rest = url + prefixLen;
    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == ' ' || rest[len - 1] == '\t' ||
                       rest[len - 1] == '\r' || rest[len - 1] == '\n')) {
        --len;
    }
    if (len < 3) return false;
    bool hasDotOrColon = false;
    for (size_t i = 0; i < len; ++i) {
        char c = rest[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return false;
        if (c == '.' || c == ':') hasDotOrColon = true;
    }
    return hasDotOrColon;
}

bool appStoreIsBinFile(const char* name) {
    if (!name) return false;
    size_t len = strlen(name);
    if (len <= 4) return false;
    const char* ext = name + (len - 4);
    if (ext[0] != '.') return false;
    auto eq = [](char a, char b) {
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        return a == b;
    };
    return eq(ext[1], 'b') && eq(ext[2], 'i') && eq(ext[3], 'n');
}

std::string appStoreNormalizeSdPath(const char* path) {
    if (!path || !path[0]) return "/";
    std::string s(path);
    // Ensure leading slash
    if (s[0] != '/') s.insert(s.begin(), '/');
    // Collapse trailing slashes (except root "/")
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

std::string appStoreParentPath(const char* path) {
    std::string norm = appStoreNormalizeSdPath(path);
    if (norm == "/") return "/";
    size_t slash = norm.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return norm.substr(0, slash);
}

void appStoreFormatSdPathDisplay(const char* path, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    std::string norm = appStoreNormalizeSdPath(path);
    if (norm == "/") {
        snprintf(out, outSize, "/ (Root)");
        return;
    }
    constexpr size_t kMaxChars = 22;
    if (norm.size() <= kMaxChars) {
        snprintf(out, outSize, "%s", norm.c_str());
    } else {
        // Keep trailing path characters with leading "..."
        const char* tail = norm.c_str() + (norm.size() - (kMaxChars - 3));
        snprintf(out, outSize, "...%s", tail);
    }
}

bool appStoreIsValidSdName(const char* name) {
    if (!name) return false;
    while (*name == ' ' || *name == '\t' || *name == '\r' || *name == '\n') ++name;
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t' ||
                       name[len - 1] == '\r' || name[len - 1] == '\n')) {
        --len;
    }
    if (len == 0 || len > 64) return false;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) return false;
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

const char* appStorePartitionKindLabel(uint8_t type, uint8_t subtype) {
    if (type == 0x00) {
        if (subtype == 0x00 || subtype == 0x20) return "System";
        return "App";
    }
    if (type == 0x01) {
        switch (subtype) {
            case 0x00: return "OTA Data";
            case 0x02: return "NVS";
            case 0x81: return "FAT";
            case 0x82: return "SPIFFS";
            case 0x83: return "LittleFS";
            default:   return "Data";
        }
    }
    return "Other";
}

const char* appStorePartitionShortTag(uint8_t type, uint8_t subtype) {
    if (type == 0x00) {
        return (subtype == 0x00 || subtype == 0x20) ? "SYS" : "APP";
    }
    if (type == 0x01) {
        switch (subtype) {
            case 0x00: return "OTA";
            case 0x02: return "NVS";
            case 0x81: return "FAT";
            case 0x82: return "SFS";
            case 0x83: return "LFS";
            default:   return "DAT";
        }
    }
    return "OTH";
}

uint32_t appStorePartitionColorHex(uint8_t type, uint8_t subtype) {
    if (type == 0x00) {
        return (subtype == 0x00 || subtype == 0x20) ? 0x4285F4u : 0x34A853u;
    }
    if (type == 0x01) {
        if (subtype == 0x81 || subtype == 0x82 || subtype == 0x83) return 0xFBBC04u;
        return 0xA78BFAu;
    }
    return 0x9AA0A6u;
}

bool appStoreIsProtectedPartition(uint8_t type, uint8_t subtype) {
    if (type == 0x00 && (subtype == 0x00 || subtype == 0x20)) return true;
    if (type == 0x01 && (subtype == 0x00 || subtype == 0x02)) return true;
    return false;
}

void appStoreFormatPartitionMeta(uint32_t offset,
                                 uint32_t sizeBytes,
                                 uint8_t type,
                                 uint8_t subtype,
                                 char* out,
                                 size_t outSize) {
    if (!out || outSize == 0) return;
    char szBuf[24];
    appStoreFormatSizeCompact(sizeBytes, szBuf, sizeof(szBuf));
    snprintf(out, outSize, "0x%06X • %s • %s",
             static_cast<unsigned>(offset), szBuf, appStorePartitionKindLabel(type, subtype));
}

uint8_t appStoreComputeSegmentWidthPct(uint32_t segmentBytes, uint32_t totalBytes) {
    if (segmentBytes == 0 || totalBytes == 0) return 0;
    uint64_t pct = (static_cast<uint64_t>(segmentBytes) * 100u + (totalBytes / 2u)) / totalBytes;
    if (pct < 1u) return 1u;
    if (pct > 100u) return 100u;
    return static_cast<uint8_t>(pct);
}

bool appStoreIsValidDeviceTag(const char* tag) {
    if (!tag) return false;
    while (*tag == ' ' || *tag == '\t') ++tag;
    size_t len = strlen(tag);
    while (len > 0 && (tag[len - 1] == ' ' || tag[len - 1] == '\t')) --len;
    if (len == 0 || len > 24) return false;
    for (size_t i = 0; i < len; ++i) {
        char c = tag[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

const char* appStoreBootPrefLabel(bool bootToApp) {
    return bootToApp ? "Last Launched App" : "Pixel CYD (App Store)";
}

void appStoreFormatWebUiTransfer(uint32_t doneBytes,
                                 uint32_t totalBytes,
                                 uint8_t pct,
                                 char* out,
                                 size_t outSize) {
    if (!out || outSize == 0) return;
    if (doneBytes == 0 && totalBytes == 0) {
        out[0] = '\0';
        return;
    }
    char doneBuf[20];
    appStoreFormatSizeCompact(doneBytes, doneBuf, sizeof(doneBuf));
    if (totalBytes > 0) {
        char totBuf[20];
        appStoreFormatSizeCompact(totalBytes, totBuf, sizeof(totBuf));
        uint8_t safePct = pct > 100 ? 100 : pct;
        snprintf(out, outSize, "%s / %s (%u%%)", doneBuf, totBuf, static_cast<unsigned>(safePct));
    } else {
        snprintf(out, outSize, "%s", doneBuf);
    }
}

AppStoreStorageSummary appStoreComputeStorageSummary(uint32_t dynamicTotalBytes,
                                                     uint32_t freeBytes,
                                                     uint8_t installedCount) {
    AppStoreStorageSummary s;
    s.dynamicTotalBytes = dynamicTotalBytes;
    s.freeBytes = (freeBytes > dynamicTotalBytes) ? dynamicTotalBytes : freeBytes;
    s.usedBytes = (dynamicTotalBytes >= s.freeBytes) ? (dynamicTotalBytes - s.freeBytes) : 0;
    if (dynamicTotalBytes > 0) {
        uint64_t pct = (static_cast<uint64_t>(s.usedBytes) * 100u + (dynamicTotalBytes / 2u)) / dynamicTotalBytes;
        s.usedPct = (pct > 100u) ? 100u : static_cast<uint8_t>(pct);
    } else {
        s.usedPct = 0;
    }
    s.installedCount = installedCount;
    return s;
}

void appStoreFormatSizeCompact(uint32_t bytes, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (bytes >= 1024u * 1024u) {
        snprintf(out, outSize, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, outSize, "%u KB", static_cast<unsigned>((bytes + 512u) / 1024u));
    } else {
        snprintf(out, outSize, "%u B", static_cast<unsigned>(bytes));
    }
}

void appStoreFormatInstalledMeta(uint32_t appSizeBytes,
                                 uint8_t dataPartitionCount,
                                 bool isDefaultBoot,
                                 bool hasBackup,
                                 char* out,
                                 size_t outSize) {
    if (!out || outSize == 0) return;
    char sizeBuf[24];
    appStoreFormatSizeCompact(appSizeBytes, sizeBuf, sizeof(sizeBuf));

    int written = snprintf(out, outSize, "%s", sizeBuf);
    if (written < 0 || static_cast<size_t>(written) >= outSize) return;
    size_t pos = static_cast<size_t>(written);

    if (dataPartitionCount > 0 && pos < outSize) {
        written = snprintf(out + pos, outSize - pos, " • %u data", (unsigned)dataPartitionCount);
        if (written > 0) pos += static_cast<size_t>(written);
    }
    if (hasBackup && pos < outSize) {
        written = snprintf(out + pos, outSize - pos, " • Backup");
        if (written > 0) pos += static_cast<size_t>(written);
    }
    if (isDefaultBoot && pos < outSize) {
        snprintf(out + pos, outSize - pos, " • Boot");
    }
}

#ifndef APP_STORE_HOST_TEST

#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_keyboard.h"
#include "../components/m3_widgets.h"
#include "../drivers/wifi_mgr.h"
#include "../ui/app_manager.h"
#include "../launcher/backup_manager.h"
#include "../launcher/install_shared.h"
#include "../launcher/launcher_app_registry.h"
#include "../launcher/launcher_home_sync.h"
#include "../launcher/launcher_ui_bridge.h"
#include "../launcher/onlineLauncher.h"
#include "../launcher/partition_table_model.h"
#include "../launcher/partitioner.h"
#include "../launcher/sd_functions.h"
#include "../launcher/webInterface.h"
#include <WiFi.h>
#include <algorithm>
#include <esp_ota_ops.h>
#include <functional>
#include <vector>

namespace {

constexpr uint8_t kTabCount = static_cast<uint8_t>(AppStoreTab::Count);

lv_obj_t* s_storeBody = nullptr;
lv_obj_t* s_tabBar = nullptr;
lv_obj_t* s_tabBtns[kTabCount] = {};
lv_obj_t* s_tabLbls[kTabCount] = {};
lv_obj_t* s_tabContent = nullptr;

AppStoreTab s_activeTab = AppStoreTab::Installed;
bool s_asyncRefreshPending = false;

enum class PendingAppAction : uint8_t {
    None = 0,
    Launch,
    Rename,
    BackupMenu,
    Uninstall
};

struct InstalledRowData {
    String label;
    String name;
    String appNum;
    std::vector<String> fatLabels;
    String spiffsLabel;
    uint32_t appSizeBytes = 0;
    uint8_t dataPartCount = 0;
    bool isDefaultBoot = false;
    bool hasBackup = false;
    uint32_t accentHex = M3Color::PRIMARY;
    uint32_t containerHex = 0x2563EB;
};

std::vector<InstalledRowData> s_installedRows;
PendingAppAction s_pendingAction = PendingAppAction::None;
char s_pendingLabel[16] = {};
char s_pendingName[24] = {};

// --- Explore Tab State ---
struct ExploreCatalogItem {
    String fid;
    String name;
    String author;
    String version;
    String file;
    bool starred = false;
};

struct ExploreVersionItem {
    String version;
    String file;
    String publishedAt;
    uint32_t fileSize = 0;
    uint32_t appSize = 0;
    uint32_t appOffset = 0;
    bool hasData = false;
};

struct ExploreDetailState {
    bool active = false;
    String fid;
    String name;
    String author;
    String category;
    bool starred = false;
    std::vector<ExploreVersionItem> versions;
    size_t selectedVersionIdx = 0;
};

enum class PendingExploreAction : uint8_t {
    None = 0,
    FetchCatalog,
    OpenDetail,
    PickVersion,
    InstallSelected,
    DownloadSelected,
    InstallUrl,
    ConnectWifi
};

AppStoreSort s_exploreSort = AppStoreSort::Popular;
String s_exploreQuery;
uint8_t s_explorePage = 1;
int s_exploreNumPages = 1;
int s_exploreTotal = 0;
bool s_exploreLoaded = false;
bool s_exploreLoading = false;
String s_exploreError;
std::vector<ExploreCatalogItem> s_exploreItems;
ExploreDetailState s_exploreDetail;

PendingExploreAction s_pendingExploreAction = PendingExploreAction::None;
size_t s_pendingExploreItemIdx = 0;
String s_pendingExploreUrl;

// Reusable m3_keyboard text prompt modal (Search Apps / Install from URL / SD Rename & New Folder)
lv_obj_t* s_promptBackdrop = nullptr;
lv_obj_t* s_promptTa = nullptr;
std::function<void(const char*)> s_promptOnSubmit;

// --- SD Card Tab State ---
enum class PendingSdAction : uint8_t {
    None = 0,
    InstallBin,
    EntryMenu,
    DeleteEntry,
    RenameEntry,
    NewFolder,
    CopyEntry,
    PasteHere
};

String s_sdCurrentPath;
bool s_sdPathInitialized = false;
bool s_sdOnlyBins = false;
std::vector<LauncherSdEntry> s_sdEntries;
PendingSdAction s_pendingSdAction = PendingSdAction::None;
size_t s_pendingSdIdx = 0;

// --- Tools Tab State (PMan, Backup/Restore, Updates, Web Upload, Settings) ---
enum class ToolsSubView : uint8_t {
    Menu = 0,
    StoragePartitions,
    BackupRestore,
    WebUpload,
    Settings
};

enum class PendingToolsAction : uint8_t {
    None = 0,
    CompactFlash,
    DumpTable,
    PartitionMenu,
    BackupAllApps,
    BackupApp,
    RestoreApp,
    ManageBackupEntry,
    CheckUpdates,
    StartWebUiSta,
    StartWebUiAp,
    StopWebUi,
    LaunchLastWuiApp,
    ResetBootToFactory,
    EditDeviceTag,
    ClearAppRegistry
};

struct ToolsBackupRow {
    String appNum;
    String appName;
    String appLabel; // non-empty when currently installed
    uint8_t dataPartCount = 0;
    bool isInstalled = false;
    bool hasBackup = false;
    std::vector<String> fatLabels;
    String spiffsLabel;
};

ToolsSubView s_toolsSubView = ToolsSubView::Menu;
std::vector<LauncherPartitionEntry> s_pmanEntries;
std::vector<ToolsBackupRow> s_backupRows;
PendingToolsAction s_pendingToolsAction = PendingToolsAction::None;
size_t s_pendingToolsIdx = 0;

// Live Web Upload (WUI) widgets & polling timer
lv_timer_t* s_wuiTimer = nullptr;
lv_obj_t* s_wuiStatusLbl = nullptr;
lv_obj_t* s_wuiProgBar = nullptr;
lv_obj_t* s_wuiTransferLbl = nullptr;
uint32_t s_wuiLastEventSeq = 0;

void renderActiveTab();
void updateTabBarStyle();
void queueExploreAction(PendingExploreAction action, size_t itemIdx = 0, const char* url = nullptr);
void queueSdAction(PendingSdAction action, size_t entryIdx = 0);
void queueToolsAction(PendingToolsAction action, size_t idx = 0);

AppStoreStorageSummary computeLiveStorageSummary(const LauncherPartitionTable* tableOpt, uint8_t installedCount) {
    LauncherPartitionTable localTable;
    const LauncherPartitionTable* table = tableOpt;
    if (!table) {
        if (launcherPartitionReadCurrent(localTable)) {
            table = &localTable;
        }
    }
    if (!table || table->flashSize == 0) {
        return appStoreComputeStorageSummary(0, 0, installedCount);
    }

    // Dynamic pool starts immediately after factory/test app partition (typically 0x210000).
    uint32_t dynamicStart = 0x210000u;
    for (const LauncherPartitionEntry& entry : table->entries) {
        if (entry.isFactoryOrTestApp()) {
            uint32_t end = entry.offset + entry.size;
            if (end > dynamicStart) dynamicStart = end;
        }
    }
    uint32_t dynamicTotal = (table->flashSize > dynamicStart) ? (table->flashSize - dynamicStart) : 0;

    uint32_t freeBytes = 0;
    std::vector<LauncherPartitionRange> freeRanges = launcherPartitionFreeRanges(*table);
    for (const LauncherPartitionRange& r : freeRanges) {
        if (r.offset >= dynamicStart) {
            freeBytes += r.size;
        } else if (r.offset + r.size > dynamicStart) {
            freeBytes += (r.offset + r.size) - dynamicStart;
        }
    }
    return appStoreComputeStorageSummary(dynamicTotal, freeBytes, installedCount);
}

const InstalledRowData* findRowByLabel(const char* label) {
    if (!label || !label[0]) return nullptr;
    for (const InstalledRowData& row : s_installedRows) {
        if (row.label == label) return &row;
    }
    return nullptr;
}

std::vector<InstalledRowData> collectInstalledRows(LauncherPartitionTable* outTable) {
    std::vector<InstalledRowData> rows;
    LauncherPartitionTable table;
    bool hasTable = launcherPartitionReadCurrent(table);
    if (outTable && hasTable) *outTable = table;

    std::vector<LauncherAppMetadata> installed = launcherListInstalledApps();
    if (installed.empty()) return rows;

    // Preserve Home Screen newest-first ordering from app_registry dynamic slots.
    std::vector<String> orderedLabels;
    uint8_t totalApps = appGetCount();
    for (uint8_t i = 0; i < totalApps; ++i) {
        const AppMeta* meta = appGetAt(i);
        if (meta && appIsDynamic(meta) && meta->launcherLabel && meta->launcherLabel[0]) {
            orderedLabels.push_back(String(meta->launcherLabel));
        }
    }
    // Append any installed app not yet in app_registry (fallback)
    for (const LauncherAppMetadata& app : installed) {
        bool seen = false;
        for (const String& l : orderedLabels) {
            if (l == app.label) {
                seen = true;
                break;
            }
        }
        if (!seen) orderedLabels.push_back(app.label);
    }

    const esp_partition_t* bootPart = esp_ota_get_boot_partition();
    String bootLabel = (bootPart && bootPart->type == ESP_PARTITION_TYPE_APP &&
                        bootPart->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0)
                           ? String(bootPart->label)
                           : String("");

    bool sdReady = isSdCardMounted();

    for (const String& lbl : orderedLabels) {
        const LauncherAppMetadata* meta = nullptr;
        for (const LauncherAppMetadata& app : installed) {
            if (app.label == lbl) {
                meta = &app;
                break;
            }
        }
        if (!meta) continue;

        InstalledRowData row;
        row.label = meta->label;
        row.name = meta->name.isEmpty() ? meta->label : meta->name;
        row.appNum = meta->appNum;
        row.fatLabels = meta->fatLabels;
        row.spiffsLabel = meta->spiffsLabel;
        row.isDefaultBoot = (!bootLabel.isEmpty() && bootLabel == row.label);
        launcherHomePickColors(row.label.c_str(), &row.accentHex, &row.containerHex);

        if (hasTable) {
            const LauncherPartitionEntry* appEntry = launcherPartitionFindByLabel(table, row.label.c_str());
            if (appEntry) row.appSizeBytes = appEntry->size;
        }

        uint8_t dataCount = static_cast<uint8_t>(row.fatLabels.size());
        if (!row.spiffsLabel.isEmpty()) dataCount++;
        row.dataPartCount = dataCount;

        if (sdReady && !row.appNum.isEmpty()) {
            BackupInstallInfo bkp = loadInstalledFromConfig(row.appNum);
            row.hasBackup = hasRestorableBackup(bkp);
        }

        rows.push_back(row);
    }
    return rows;
}

bool ensureBackupConfigForRow(const InstalledRowData& row, String& outAppNum) {
    outAppNum = row.appNum;
    if (outAppNum.isEmpty()) {
        outAppNum = launcherGenerateAppNum(String("app:") + row.label);
    }

    BackupInstallInfo existing = loadInstalledFromConfig(outAppNum);
    if (!existing.appNum.isEmpty() && !existing.partitions.empty()) {
        return true;
    }

    // Build backup metadata from NVS-linked FAT/SPIFFS partitions.
    LauncherPartitionTable table;
    launcherPartitionReadCurrent(table);

    BackupInstallInfo info;
    info.appNum = outAppNum;
    info.appName = row.name;
    info.sdFilepath = existing.sdFilepath;

    for (const String& fatLbl : row.fatLabels) {
        if (fatLbl.isEmpty()) continue;
        BackupPartitionInfo p;
        p.type = "FAT";
        p.label = fatLbl;
        info.partitions.push_back(p);
    }
    if (!row.spiffsLabel.isEmpty()) {
        BackupPartitionInfo p;
        p.label = row.spiffsLabel;
        p.type = "SPIFFS";
        const LauncherPartitionEntry* entry = launcherPartitionFindByLabel(table, row.spiffsLabel.c_str());
        if (entry && entry->subtype == 0x83) {
            p.type = "LittleFS";
        }
        info.partitions.push_back(p);
    }

    if (info.partitions.empty()) {
        return false;
    }
    return saveInstalledToConfig(info);
}

void runBackupMenuForRow(const InstalledRowData& row) {
    if (!setupSdCard()) {
        launcherUiError("Insert a FAT32 SD card to back up or restore App data.", "SD Card Required");
        return;
    }

    String appNum = row.appNum;
    if (appNum.isEmpty()) {
        appNum = launcherGenerateAppNum(String("app:") + row.label);
    }

    BackupInstallInfo existing = loadInstalledFromConfig(appNum);
    bool canRestore = hasRestorableBackup(existing);
    bool hasDataPartitions = (row.dataPartCount > 0) || !existing.partitions.empty();

    if (!hasDataPartitions && !canRestore) {
        launcherUiInfo(row.name + " has no separate data partitions (SPIFFS/FAT/LittleFS) to back up.", "App Data");
        return;
    }

    String subtitle = row.name + " (" + row.label + ")";
    std::vector<LauncherUiOption> opts;

    if (hasDataPartitions) {
        opts.push_back({String(LV_SYMBOL_SAVE "  Back Up App Data to SD"), [row]() {
            String num;
            if (!ensureBackupConfigForRow(row, num)) {
                launcherUiInfo("No data partitions registered for " + row.name + ".", "Backup");
                return;
            }
            if (!backupAllPartitionsForApp(num)) {
                launcherUiError("Could not complete backup for " + row.name + ".", "Backup Failed");
            }
            appStoreNotifyAppsChanged();
        }});
    }

    if (canRestore) {
        opts.push_back({String(LV_SYMBOL_REFRESH "  Restore Last Backup"), [row, appNum]() {
            String msg = String("Overwrite ") + row.name + "'s flash data with its latest SD backup?";
            if (!launcherUiConfirm("Restore Data", msg.c_str(), "Restore", M3Color::PRIMARY)) {
                return;
            }
            if (!restoreLastBackupForApp(appNum)) {
                launcherUiError("Could not restore backup for " + row.name + ".", "Restore Failed");
            }
        }});
    }

    launcherUiSelectOption("App Data Backup", opts, subtitle.c_str());
}

void asyncDispatchAppAction(void* arg) {
    (void)arg;
    PendingAppAction action = s_pendingAction;
    String label(s_pendingLabel);
    String name(s_pendingName);
    s_pendingAction = PendingAppAction::None;
    s_pendingLabel[0] = '\0';
    s_pendingName[0] = '\0';

    if (action == PendingAppAction::None || label.isEmpty()) return;

    switch (action) {
        case PendingAppAction::Launch:
            launcherBootAppByLabel(label.c_str());
            break;
        case PendingAppAction::Rename:
            launcherHomePromptRename(label.c_str(), name.c_str());
            break;
        case PendingAppAction::BackupMenu: {
            const InstalledRowData* row = findRowByLabel(label.c_str());
            if (row) {
                InstalledRowData copy = *row;
                runBackupMenuForRow(copy);
            }
            break;
        }
        case PendingAppAction::Uninstall:
            launcherDeleteAppByLabel(label.c_str(), false, false);
            break;
        default:
            break;
    }
}

void queueAppAction(PendingAppAction action, const InstalledRowData* row) {
    if (!row || row->label.isEmpty()) return;
    s_pendingAction = action;
    strncpy(s_pendingLabel, row->label.c_str(), sizeof(s_pendingLabel) - 1);
    s_pendingLabel[sizeof(s_pendingLabel) - 1] = '\0';
    strncpy(s_pendingName, row->name.c_str(), sizeof(s_pendingName) - 1);
    s_pendingName[sizeof(s_pendingName) - 1] = '\0';
    lv_async_call(asyncDispatchAppAction, nullptr);
}

void installedActionBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    uintptr_t packed = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    PendingAppAction action = static_cast<PendingAppAction>((packed >> 8) & 0xFFu);
    size_t idx = static_cast<size_t>(packed & 0xFFu);
    if (idx >= s_installedRows.size()) return;
    queueAppAction(action, &s_installedRows[idx]);
}

void syncRefreshBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    launcherHomeSyncRefresh(true, false);
    renderActiveTab();
    m3ShowToast("Synced with Home Screen", 1200);
}

void switchTabBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    AppStoreTab next = appStoreTabFromIndex(idx);
    if (next == s_activeTab) {
        if (next == AppStoreTab::Explore && s_exploreDetail.active) {
            s_exploreDetail.active = false;
            renderActiveTab();
        } else if (next == AppStoreTab::SdCard && isSdCardMounted()) {
            String homePath = SD.exists("/downloads") ? String("/downloads") : String("/");
            if (s_sdCurrentPath != homePath) {
                s_sdCurrentPath = homePath;
                renderActiveTab();
            }
        } else if (next == AppStoreTab::Tools && s_toolsSubView != ToolsSubView::Menu) {
            s_toolsSubView = ToolsSubView::Menu;
            renderActiveTab();
        }
        return;
    }
    s_activeTab = next;
    updateTabBarStyle();
    renderActiveTab();
}

void openWifiSettingsCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    appManagerLaunchById("wifi");
}

void checkSdBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    bool ok = setupSdCard();
    m3ShowToast(ok ? "SD card mounted" : "No SD card detected", 1300);
    renderActiveTab();
}

lv_obj_t* createCompactChip(lv_obj_t* parent, const char* text, uint32_t bgHex, uint32_t fgHex,
                            lv_event_cb_t cb, void* userData) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_height(btn, 26);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fgHex), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(lbl);
    return btn;
}

void buildInstalledTab(lv_obj_t* container) {
    LauncherPartitionTable table;
    s_installedRows = collectInstalledRows(&table);
    AppStoreStorageSummary storage = computeLiveStorageSummary(
        table.flashSize > 0 ? &table : nullptr,
        static_cast<uint8_t>(s_installedRows.size())
    );

    // 1. Compact storage + sync summary card
    lv_obj_t* hdrCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(hdrCard, 8, 0);
    lv_obj_set_style_pad_row(hdrCard, 5, 0);
    lv_obj_set_flex_flow(hdrCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* topRow = lv_obj_create(hdrCard);
    lv_obj_set_size(topRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    char countBuf[48];
    if (storage.installedCount == 0) {
        snprintf(countBuf, sizeof(countBuf), "No Apps installed");
    } else {
        snprintf(countBuf, sizeof(countBuf), "%u %s installed",
                 (unsigned)storage.installedCount,
                 (storage.installedCount == 1) ? "App" : "Apps");
    }

    lv_obj_t* countLbl = lv_label_create(topRow);
    lv_label_set_text(countLbl, countBuf);
    lv_obj_set_style_text_color(countLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(countLbl, &lv_font_montserrat_12, 0);

    lv_obj_t* rightGroup = lv_obj_create(topRow);
    lv_obj_set_size(rightGroup, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rightGroup, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightGroup, 0, 0);
    lv_obj_set_style_pad_all(rightGroup, 0, 0);
    lv_obj_set_style_pad_column(rightGroup, 6, 0);
    lv_obj_set_flex_flow(rightGroup, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rightGroup, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rightGroup, LV_OBJ_FLAG_SCROLLABLE);

    char freeBuf[28];
    char freeSz[16];
    appStoreFormatSizeCompact(storage.freeBytes, freeSz, sizeof(freeSz));
    snprintf(freeBuf, sizeof(freeBuf), "%s free", freeSz);

    lv_obj_t* freeLbl = lv_label_create(rightGroup);
    lv_label_set_text(freeLbl, freeBuf);
    lv_obj_set_style_text_color(freeLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(freeLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* syncBtn = lv_btn_create(rightGroup);
    lv_obj_set_size(syncBtn, 26, 24);
    lv_obj_set_style_radius(syncBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(syncBtn, lv_color_hex(M3Color::PRIMARY_CONT), 0);
    lv_obj_set_style_shadow_width(syncBtn, 0, 0);
    lv_obj_set_style_pad_all(syncBtn, 0, 0);
    lv_obj_add_event_cb(syncBtn, syncRefreshBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* syncIco = lv_label_create(syncBtn);
    lv_label_set_text(syncIco, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(syncIco, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_text_font(syncIco, &lv_font_montserrat_10, 0);
    lv_obj_center(syncIco);

    lv_obj_t* bar = lv_bar_create(hdrCard);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, storage.usedPct, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, M3Dim::RADIUS_PILL, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, M3Dim::RADIUS_PILL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(storage.usedPct >= 85 ? M3Color::CORAL : m3PrimaryHex()),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    // 2. Empty state when no Apps are installed
    if (s_installedRows.empty()) {
        lv_obj_t* emptyCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
        lv_obj_set_flex_flow(emptyCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(emptyCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(emptyCard, 12, 0);
        lv_obj_set_style_pad_row(emptyCard, 6, 0);

        lv_obj_t* ico = lv_label_create(emptyCard);
        lv_label_set_text(ico, LV_SYMBOL_DOWNLOAD);
        lv_obj_set_style_text_color(ico, lv_color_hex(m3PrimaryHex()), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

        lv_obj_t* title = lv_label_create(emptyCard);
        lv_label_set_text(title, "No Apps Installed Yet");
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

        lv_obj_t* desc = lv_label_create(emptyCard);
        lv_label_set_text(desc, "Install Apps from Explore or SD Card. New Apps appear at slot #1 on your Home Screen.");
        lv_obj_set_width(desc, LV_PCT(100));
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);

        lv_obj_t* btnRow = lv_obj_create(emptyCard);
        lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, 6, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

        createCompactChip(btnRow, LV_SYMBOL_WIFI " Explore", M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                          switchTabBtnCb, reinterpret_cast<void*>(static_cast<intptr_t>(AppStoreTab::Explore)));
        createCompactChip(btnRow, LV_SYMBOL_SD_CARD " SD Card", m3SurfaceHighestHex(), m3TextPrimaryHex(),
                          switchTabBtnCb, reinterpret_cast<void*>(static_cast<intptr_t>(AppStoreTab::SdCard)));
        return;
    }

    // 3. Installed App cards (newest-first, synced with Home Screen)
    for (size_t i = 0; i < s_installedRows.size(); ++i) {
        const InstalledRowData& row = s_installedRows[i];

        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_pad_row(card, 7, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

        // Top row: icon badge + title/meta + Launch pill
        lv_obj_t* infoRow = lv_obj_create(card);
        lv_obj_set_size(infoRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(infoRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(infoRow, 0, 0);
        lv_obj_set_style_pad_all(infoRow, 0, 0);
        lv_obj_set_style_pad_column(infoRow, 8, 0);
        lv_obj_set_flex_flow(infoRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(infoRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(infoRow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* badge = lv_obj_create(infoRow);
        lv_obj_set_size(badge, 30, 30);
        lv_obj_set_style_radius(badge, 10, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(row.containerHex), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* badgeIco = lv_label_create(badge);
        lv_label_set_text(badgeIco, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(badgeIco, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(badgeIco, &lv_font_montserrat_12, 0);
        lv_obj_center(badgeIco);

        lv_obj_t* textCol = lv_obj_create(infoRow);
        lv_obj_set_height(textCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textCol, 1);
        lv_obj_set_style_bg_opa(textCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textCol, 0, 0);
        lv_obj_set_style_pad_all(textCol, 0, 0);
        lv_obj_set_style_pad_row(textCol, 2, 0);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* nameLbl = lv_label_create(textCol);
        lv_label_set_text(nameLbl, row.name.c_str());
        lv_obj_set_width(nameLbl, LV_PCT(100));
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_14, 0);

        char metaBuf[64];
        appStoreFormatInstalledMeta(row.appSizeBytes, row.dataPartCount,
                                    row.isDefaultBoot, row.hasBackup,
                                    metaBuf, sizeof(metaBuf));
        lv_obj_t* metaLbl = lv_label_create(textCol);
        lv_label_set_text(metaLbl, metaBuf);
        lv_obj_set_width(metaLbl, LV_PCT(100));
        lv_label_set_long_mode(metaLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(metaLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(metaLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* launchBtn = m3PillBtn(infoRow, LV_SYMBOL_PLAY " Run",
                                        M3Color::PRIMARY, M3Color::ON_PRIMARY, 56, 28);
        uintptr_t launchArg = (static_cast<uintptr_t>(PendingAppAction::Launch) << 8) | (i & 0xFFu);
        lv_obj_add_event_cb(launchBtn, installedActionBtnCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(launchArg));

        // Action row: Rename | Backup | Uninstall
        lv_obj_t* actRow = lv_obj_create(card);
        lv_obj_set_size(actRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(actRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(actRow, 0, 0);
        lv_obj_set_style_pad_all(actRow, 0, 0);
        lv_obj_set_style_pad_column(actRow, 5, 0);
        lv_obj_set_flex_flow(actRow, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(actRow, LV_OBJ_FLAG_SCROLLABLE);

        uintptr_t renameArg = (static_cast<uintptr_t>(PendingAppAction::Rename) << 8) | (i & 0xFFu);
        uintptr_t backupArg = (static_cast<uintptr_t>(PendingAppAction::BackupMenu) << 8) | (i & 0xFFu);
        uintptr_t deleteArg = (static_cast<uintptr_t>(PendingAppAction::Uninstall) << 8) | (i & 0xFFu);

        createCompactChip(actRow, LV_SYMBOL_EDIT " Rename",
                          m3SurfaceHighestHex(), m3TextPrimaryHex(),
                          installedActionBtnCb, reinterpret_cast<void*>(renameArg));
        createCompactChip(actRow, LV_SYMBOL_SAVE " Backup",
                          M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                          installedActionBtnCb, reinterpret_cast<void*>(backupArg));
        createCompactChip(actRow, LV_SYMBOL_TRASH " Remove",
                          0x4A2024, M3Color::CORAL,
                          installedActionBtnCb, reinterpret_cast<void*>(deleteArg));
    }
}

// --- Reusable m3_keyboard Prompt Modal (Explore Search & Install from URL) ---

void closeTextPromptModal() {
    m3KeyboardHide();
    if (s_promptBackdrop) {
        lv_obj_del(s_promptBackdrop);
        s_promptBackdrop = nullptr;
    }
    s_promptTa = nullptr;
    s_promptOnSubmit = nullptr;
}

void submitTextPrompt(const char* raw) {
    auto cb = s_promptOnSubmit;
    String val(raw ? raw : "");
    closeTextPromptModal();
    if (cb) cb(val.c_str());
}

void textPromptKbdEnterCb(const char* text) {
    submitTextPrompt(text);
}

void textPromptConfirmBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    const char* txt = s_promptTa ? lv_textarea_get_text(s_promptTa) : "";
    submitTextPrompt(txt);
}

void textPromptCancelBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    closeTextPromptModal();
}

void openTextPromptModal(const char* title,
                         const char* placeholder,
                         const char* initialText,
                         uint16_t maxLen,
                         const char* confirmLabel,
                         std::function<void(const char*)> onSubmit) {
    closeTextPromptModal();
    s_promptOnSubmit = std::move(onSubmit);

    lv_obj_t* top = lv_layer_top();
    s_promptBackdrop = lv_obj_create(top);
    lv_obj_set_size(s_promptBackdrop, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_set_pos(s_promptBackdrop, 0, 0);
    lv_obj_set_style_bg_color(s_promptBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_promptBackdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_promptBackdrop, 0, 0);
    lv_obj_set_style_radius(s_promptBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_promptBackdrop, 0, 0);
    lv_obj_clear_flag(s_promptBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_promptBackdrop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* card = lv_obj_create(s_promptBackdrop);
    lv_obj_set_size(card, 216, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_color(card, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, title ? title : "Input");
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);

    s_promptTa = lv_textarea_create(card);
    lv_obj_set_width(s_promptTa, LV_PCT(100));
    lv_textarea_set_one_line(s_promptTa, true);
    lv_textarea_set_max_length(s_promptTa, maxLen);
    if (placeholder && placeholder[0]) lv_textarea_set_placeholder_text(s_promptTa, placeholder);
    if (initialText && initialText[0]) lv_textarea_set_text(s_promptTa, initialText);
    lv_obj_set_style_bg_color(s_promptTa, lv_color_hex(m3SurfaceHex()), 0);
    lv_obj_set_style_text_color(s_promptTa, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_border_color(s_promptTa, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_border_width(s_promptTa, 1, 0);
    lv_obj_set_style_radius(s_promptTa, 10, 0);
    lv_obj_set_style_text_font(s_promptTa, &lv_font_montserrat_12, 0);

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancelBtn = m3PillBtn(row, "Cancel", m3SurfaceHighestHex(), m3TextPrimaryHex(), 68, 28);
    lv_obj_add_event_cb(cancelBtn, textPromptCancelBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* okBtn = m3PillBtn(row, confirmLabel ? confirmLabel : "OK",
                                M3Color::PRIMARY, M3Color::ON_PRIMARY, 76, 28);
    lv_obj_add_event_cb(okBtn, textPromptConfirmBtnCb, LV_EVENT_CLICKED, nullptr);

    m3KeyboardShow(s_promptTa, textPromptKbdEnterCb);
}

// --- Explore Tab Logic (LauncherHub Catalog, Detail, Version Picker, URL Install) ---

void fetchExploreCatalogNow() {
    if (s_exploreLoading) return;
    if (!launcherIsWifiConnected()) {
        s_exploreError = "Wi-Fi not connected";
        s_exploreLoaded = false;
        if (s_storeBody && s_activeTab == AppStoreTab::Explore) renderActiveTab();
        return;
    }

    s_exploreLoading = true;
    s_exploreError = "";

    String stepMsg = s_exploreQuery.isEmpty()
                         ? (String("Loading ") + appStoreSortLabel(s_exploreSort) + " Apps...")
                         : (String("Searching \"") + s_exploreQuery + "\"...");
    launcherUiBeginProgress("Explore", stepMsg.c_str());

    String err;
    bool ok = GetJsonFromLauncherHub(
        s_explorePage,
        String(appStoreSortApiOrder(s_exploreSort)),
        appStoreSortStarOnly(s_exploreSort),
        s_exploreQuery,
        &err
    );
    launcherUiClose();

    s_exploreLoading = false;
    s_exploreItems.clear();

    if (ok) {
        JsonArray items = g_launcherHubDoc["items"].as<JsonArray>();
        for (JsonObject it : items) {
            ExploreCatalogItem row;
            row.fid = it["fid"].is<const char*>() ? String(it["fid"].as<const char*>()) : String();
            row.name = it["name"].is<const char*>() ? String(it["name"].as<const char*>()) : String("Unnamed App");
            row.author = it["author"].is<const char*>() ? String(it["author"].as<const char*>()) : String("Community");
            row.version = it["version"].is<const char*>() ? String(it["version"].as<const char*>()) : String();
            row.file = it["file"].is<const char*>() ? String(it["file"].as<const char*>()) : String();
            row.starred = (it["star"] | 0) != 0;
            if (!row.fid.isEmpty()) {
                s_exploreItems.push_back(row);
            }
        }
        s_exploreTotal = g_launcherTotalFirmware;
        s_exploreNumPages = g_launcherNumPages < 1 ? 1 : g_launcherNumPages;
        s_explorePage = g_launcherCurrentPage < 1 ? 1 : g_launcherCurrentPage;
        s_exploreLoaded = true;
        s_exploreError = "";
        g_launcherHubDoc.clear();
    } else {
        s_exploreLoaded = true;
        s_exploreError = err.isEmpty() ? String("Could not load LauncherHub catalog") : err;
    }

    if (s_storeBody && s_activeTab == AppStoreTab::Explore) {
        renderActiveTab();
    }
}

void openExploreDetailNow(size_t idx) {
    if (idx >= s_exploreItems.size()) return;
    ExploreCatalogItem item = s_exploreItems[idx];

    launcherUiBeginProgress(item.name.c_str(), "Loading versions from LauncherHub...");
    String err;
    JsonDocument doc = getVersionInfo(item.fid, &err);
    launcherUiClose();

    if (doc.isNull() || !doc["versions"].is<JsonArray>()) {
        launcherUiError(err.isEmpty() ? String("Could not load App details") : err, "LauncherHub");
        return;
    }

    ExploreDetailState detail;
    detail.active = true;
    detail.fid = doc["fid"].is<const char*>() ? String(doc["fid"].as<const char*>()) : item.fid;
    detail.name = doc["name"].is<const char*>() ? String(doc["name"].as<const char*>()) : item.name;
    detail.author = doc["author"].is<const char*>() ? String(doc["author"].as<const char*>()) : item.author;
    detail.category = doc["category"].is<const char*>() ? String(doc["category"].as<const char*>()) : g_launcherOtaTag;
    detail.starred = (doc["star"] | (item.starred ? 1 : 0)) != 0;

    JsonArray versions = doc["versions"].as<JsonArray>();
    for (JsonObject v : versions) {
        ExploreVersionItem vi;
        vi.version = v["version"].is<const char*>() ? String(v["version"].as<const char*>()) : String();
        vi.file = v["file"].is<const char*>() ? String(v["file"].as<const char*>()) : String();
        vi.publishedAt = v["published_at"].is<const char*>() ? String(v["published_at"].as<const char*>()) : String();
        vi.fileSize = v["Fs"] | 0u;
        vi.appSize = v["as"] | 0u;
        vi.appOffset = v["ao"] | 0u;
        uint32_t sVal = v["s"] | 0u;
        uint32_t fVal = v["f"] | 0u;
        uint32_t f2Val = v["f2"] | 0u;
        vi.hasData = (sVal > 0u || fVal > 0u || f2Val > 0u);
        if (!vi.version.isEmpty() || !vi.file.isEmpty()) {
            detail.versions.push_back(vi);
        }
    }

    if (detail.versions.empty()) {
        launcherUiError("No installable versions found for this App.", detail.name.c_str());
        return;
    }

    detail.selectedVersionIdx = 0;
    s_exploreDetail = detail;
    if (s_storeBody && s_activeTab == AppStoreTab::Explore) {
        renderActiveTab();
    }
}

void pickExploreVersionNow() {
    if (!s_exploreDetail.active || s_exploreDetail.versions.empty()) return;
    std::vector<LauncherUiOption> opts;
    opts.reserve(s_exploreDetail.versions.size());
    for (size_t i = 0; i < s_exploreDetail.versions.size(); ++i) {
        const ExploreVersionItem& v = s_exploreDetail.versions[i];
        char metaBuf[64];
        appStoreFormatVersionMeta(v.appSize, v.fileSize, v.publishedAt.c_str(), v.hasData,
                                  metaBuf, sizeof(metaBuf));
        String label = (i == s_exploreDetail.selectedVersionIdx ? String(LV_SYMBOL_OK "  ") : String("")) +
                       "v" + v.version + "  (" + metaBuf + ")";
        opts.push_back({label, [i]() {
            s_exploreDetail.selectedVersionIdx = i;
        }});
    }
    int chosen = launcherUiSelectOption("Select Version", opts, s_exploreDetail.name.c_str());
    if (chosen >= 0 && s_storeBody && s_activeTab == AppStoreTab::Explore) {
        renderActiveTab();
    }
}

void asyncDispatchExploreAction(void* arg) {
    (void)arg;
    PendingExploreAction action = s_pendingExploreAction;
    size_t idx = s_pendingExploreItemIdx;
    String url = s_pendingExploreUrl;
    s_pendingExploreAction = PendingExploreAction::None;
    s_pendingExploreItemIdx = 0;
    s_pendingExploreUrl = "";

    switch (action) {
        case PendingExploreAction::FetchCatalog:
            fetchExploreCatalogNow();
            break;
        case PendingExploreAction::OpenDetail:
            openExploreDetailNow(idx);
            break;
        case PendingExploreAction::PickVersion:
            pickExploreVersionNow();
            break;
        case PendingExploreAction::InstallSelected: {
            if (!s_exploreDetail.active ||
                s_exploreDetail.selectedVersionIdx >= s_exploreDetail.versions.size()) {
                return;
            }
            const ExploreVersionItem& v = s_exploreDetail.versions[s_exploreDetail.selectedVersionIdx];
            installFirmwareFromManifest(s_exploreDetail.fid, v.version, s_exploreDetail.name, false);
            break;
        }
        case PendingExploreAction::DownloadSelected: {
            if (!s_exploreDetail.active ||
                s_exploreDetail.selectedVersionIdx >= s_exploreDetail.versions.size()) {
                return;
            }
            const ExploreVersionItem& v = s_exploreDetail.versions[s_exploreDetail.selectedVersionIdx];
            String fileName = s_exploreDetail.name + "_" + v.version;
            downloadFirmware(s_exploreDetail.fid, v.file, fileName, g_launcherDwnPath, v.version);
            break;
        }
        case PendingExploreAction::InstallUrl:
            if (!url.isEmpty()) {
                installExtFirmware(url, false);
            }
            break;
        case PendingExploreAction::ConnectWifi:
            if (ensureWifiConnected()) {
                s_exploreLoaded = false;
                s_exploreError = "";
                fetchExploreCatalogNow();
            }
            break;
        default:
            break;
    }
}

void queueExploreAction(PendingExploreAction action, size_t itemIdx, const char* url) {
    s_pendingExploreAction = action;
    s_pendingExploreItemIdx = itemIdx;
    s_pendingExploreUrl = url ? String(url) : String();
    lv_async_call(asyncDispatchExploreAction, nullptr);
}

void exploreSortChipCb(lv_event_t* e) {
    m3PulseFeedback();
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    AppStoreSort next = appStoreSortFromIndex(idx);
    if (next == s_exploreSort && s_exploreLoaded && s_exploreError.isEmpty()) return;
    s_exploreSort = next;
    s_explorePage = 1;
    s_exploreLoaded = false;
    s_exploreError = "";
    renderActiveTab();
    queueExploreAction(PendingExploreAction::FetchCatalog);
}

void exploreSearchBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    openTextPromptModal("Search Apps", "e.g. Bruce, Marauder...", s_exploreQuery.c_str(), 48, "Search",
                        [](const char* text) {
                            String q(text ? text : "");
                            q.trim();
                            s_exploreQuery = q;
                            s_explorePage = 1;
                            s_exploreLoaded = false;
                            s_exploreError = "";
                            if (s_storeBody && s_activeTab == AppStoreTab::Explore) renderActiveTab();
                            queueExploreAction(PendingExploreAction::FetchCatalog);
                        });
}

void exploreClearSearchCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_exploreQuery = "";
    s_explorePage = 1;
    s_exploreLoaded = false;
    s_exploreError = "";
    renderActiveTab();
    queueExploreAction(PendingExploreAction::FetchCatalog);
}

void exploreUrlBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    openTextPromptModal("Install from URL", "https://.../app.bin", "https://", 180, "Install",
                        [](const char* text) {
                            String u(text ? text : "");
                            u.trim();
                            if (!appStoreIsValidInstallUrl(u.c_str())) {
                                launcherUiError("Enter a valid http:// or https:// .bin URL.", "Invalid URL");
                                return;
                            }
                            queueExploreAction(PendingExploreAction::InstallUrl, 0, u.c_str());
                        });
}

void exploreItemCardCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (idx >= s_exploreItems.size()) return;
    queueExploreAction(PendingExploreAction::OpenDetail, idx);
}

void explorePageBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    int target = static_cast<int>(s_explorePage) + delta;
    if (target < 1) target = 1;
    if (target > s_exploreNumPages) target = s_exploreNumPages;
    if (target == static_cast<int>(s_explorePage)) return;
    s_explorePage = static_cast<uint8_t>(target);
    s_exploreLoaded = false;
    s_exploreError = "";
    renderActiveTab();
    queueExploreAction(PendingExploreAction::FetchCatalog);
}

void exploreRetryBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_exploreLoaded = false;
    s_exploreError = "";
    renderActiveTab();
    queueExploreAction(PendingExploreAction::FetchCatalog);
}

void exploreConnectWifiBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (wifiMgrHasSavedCreds()) {
        queueExploreAction(PendingExploreAction::ConnectWifi);
    } else {
        appManagerLaunchById("wifi");
    }
}

void exploreDetailBackCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_exploreDetail.active = false;
    renderActiveTab();
}

void explorePickVersionCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueExploreAction(PendingExploreAction::PickVersion);
}

void exploreInstallSelectedCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueExploreAction(PendingExploreAction::InstallSelected);
}

void exploreDownloadSelectedCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueExploreAction(PendingExploreAction::DownloadSelected);
}

void buildExploreDetailView(lv_obj_t* container) {
    // 1. Back bar + device tag
    lv_obj_t* navRow = lv_obj_create(container);
    lv_obj_set_size(navRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(navRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(navRow, 0, 0);
    lv_obj_set_style_pad_all(navRow, 0, 0);
    lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = m3PillBtn(navRow, LV_SYMBOL_LEFT " Explore",
                                  m3SurfaceHighestHex(), m3TextPrimaryHex(), 86, 26);
    lv_obj_add_event_cb(backBtn, exploreDetailBackCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* tagLbl = lv_label_create(navRow);
    lv_label_set_text(tagLbl, s_exploreDetail.category.c_str());
    lv_obj_set_style_text_color(tagLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(tagLbl, &lv_font_montserrat_10, 0);

    // 2. Hero App card
    uint32_t accentHex = M3Color::PRIMARY;
    uint32_t containerHex = 0x2563EB;
    launcherHomePickColors(s_exploreDetail.fid.c_str(), &accentHex, &containerHex);

    lv_obj_t* hero = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_style_pad_all(hero, 10, 0);
    lv_obj_set_style_pad_column(hero, 10, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* badge = lv_obj_create(hero);
    lv_obj_set_size(badge, 38, 38);
    lv_obj_set_style_radius(badge, 12, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(containerHex), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* badgeIco = lv_label_create(badge);
    lv_label_set_text(badgeIco, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(badgeIco, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(badgeIco, &lv_font_montserrat_16, 0);
    lv_obj_center(badgeIco);

    lv_obj_t* heroCol = lv_obj_create(hero);
    lv_obj_set_height(heroCol, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(heroCol, 1);
    lv_obj_set_style_bg_opa(heroCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heroCol, 0, 0);
    lv_obj_set_style_pad_all(heroCol, 0, 0);
    lv_obj_set_style_pad_row(heroCol, 2, 0);
    lv_obj_set_flex_flow(heroCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(heroCol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* nameLbl = lv_label_create(heroCol);
    lv_label_set_text(nameLbl, s_exploreDetail.name.c_str());
    lv_obj_set_width(nameLbl, LV_PCT(100));
    lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_14, 0);

    String authorStr = String("by ") + s_exploreDetail.author;
    if (s_exploreDetail.starred) authorStr += "  •  Featured";
    lv_obj_t* authorLbl = lv_label_create(heroCol);
    lv_label_set_text(authorLbl, authorStr.c_str());
    lv_obj_set_width(authorLbl, LV_PCT(100));
    lv_label_set_long_mode(authorLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(authorLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(authorLbl, &lv_font_montserrat_10, 0);

    // 3. Version Picker Card
    if (s_exploreDetail.selectedVersionIdx >= s_exploreDetail.versions.size()) {
        s_exploreDetail.selectedVersionIdx = 0;
    }
    const ExploreVersionItem& ver = s_exploreDetail.versions[s_exploreDetail.selectedVersionIdx];

    lv_obj_t* verCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(verCard, 9, 0);
    lv_obj_set_style_pad_row(verCard, 5, 0);
    lv_obj_set_flex_flow(verCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* verTop = lv_obj_create(verCard);
    lv_obj_set_size(verTop, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(verTop, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(verTop, 0, 0);
    lv_obj_set_style_pad_all(verTop, 0, 0);
    lv_obj_set_flex_flow(verTop, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(verTop, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(verTop, LV_OBJ_FLAG_SCROLLABLE);

    String verTitle = String("Version ") + (ver.version.isEmpty() ? "Latest" : ver.version);
    lv_obj_t* verLbl = lv_label_create(verTop);
    lv_label_set_text(verLbl, verTitle.c_str());
    lv_obj_set_style_text_color(verLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(verLbl, &lv_font_montserrat_12, 0);

    if (s_exploreDetail.versions.size() > 1) {
        char pickBuf[24];
        snprintf(pickBuf, sizeof(pickBuf), "Pick (%u)", (unsigned)s_exploreDetail.versions.size());
        lv_obj_t* pickBtn = m3PillBtn(verTop, pickBuf, M3Color::PRIMARY_CONT, M3Color::PRIMARY, 68, 24);
        lv_obj_add_event_cb(pickBtn, explorePickVersionCb, LV_EVENT_CLICKED, nullptr);
    }

    char metaBuf[64];
    appStoreFormatVersionMeta(ver.appSize, ver.fileSize, ver.publishedAt.c_str(), ver.hasData,
                              metaBuf, sizeof(metaBuf));
    lv_obj_t* verMeta = lv_label_create(verCard);
    lv_label_set_text(verMeta, metaBuf);
    lv_obj_set_width(verMeta, LV_PCT(100));
    lv_label_set_long_mode(verMeta, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(verMeta, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(verMeta, &lv_font_montserrat_10, 0);

    // 4. Primary Install & Download to SD actions
    lv_obj_t* installBtn = m3PillBtn(container, LV_SYMBOL_DOWNLOAD "  Install App",
                                     M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 34);
    lv_obj_add_event_cb(installBtn, exploreInstallSelectedCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* sdBtn = m3PillBtn(container, LV_SYMBOL_SD_CARD "  Download to SD",
                                M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 32);
    lv_obj_add_event_cb(sdBtn, exploreDownloadSelectedCb, LV_EVENT_CLICKED, nullptr);
}

void buildExploreTab(lv_obj_t* container) {
    if (s_exploreDetail.active) {
        buildExploreDetailView(container);
        return;
    }

    // 1. Wi-Fi offline state
    if (!launcherIsWifiConnected()) {
        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 6, 0);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(ico, lv_color_hex(m3PrimaryHex()), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "Connect to Browse Apps");
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

        char subBuf[80];
        snprintf(subBuf, sizeof(subBuf), "LauncherHub requires Wi-Fi (Tag: %s).", g_launcherOtaTag.c_str());
        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, subBuf);
        lv_obj_set_width(sub, LV_PCT(100));
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);

        lv_obj_t* wifiBtn = m3PillBtn(card,
                                      wifiMgrHasSavedCreds() ? LV_SYMBOL_WIFI "  Reconnect Wi-Fi"
                                                             : LV_SYMBOL_WIFI "  Open Wi-Fi Settings",
                                      M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 32);
        lv_obj_add_event_cb(wifiBtn, exploreConnectWifiBtnCb, LV_EVENT_CLICKED, nullptr);

        if (wifiMgrHasSavedCreds()) {
            lv_obj_t* settingsBtn = m3PillBtn(card, LV_SYMBOL_SETTINGS "  Wi-Fi Settings",
                                              m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 28);
            lv_obj_add_event_cb(settingsBtn, openWifiSettingsCb, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }

    // 2. Search pill + Install from URL button
    lv_obj_t* searchRow = lv_obj_create(container);
    lv_obj_set_size(searchRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(searchRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(searchRow, 0, 0);
    lv_obj_set_style_pad_all(searchRow, 0, 0);
    lv_obj_set_style_pad_column(searchRow, 5, 0);
    lv_obj_set_flex_flow(searchRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(searchRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(searchRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* searchBtn = lv_btn_create(searchRow);
    lv_obj_set_height(searchBtn, 28);
    lv_obj_set_flex_grow(searchBtn, 1);
    lv_obj_set_style_bg_color(searchBtn, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(searchBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(searchBtn, 1, 0);
    lv_obj_set_style_border_color(searchBtn, lv_color_hex(s_exploreQuery.isEmpty() ? m3OutlineHex() : m3PrimaryHex()), 0);
    lv_obj_set_style_radius(searchBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(searchBtn, 0, 0);
    lv_obj_set_style_pad_hor(searchBtn, 8, 0);
    lv_obj_set_style_pad_ver(searchBtn, 0, 0);
    lv_obj_add_event_cb(searchBtn, exploreSearchBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* searchLbl = lv_label_create(searchBtn);
    String searchTxt = s_exploreQuery.isEmpty()
                           ? String(LV_SYMBOL_EYE_OPEN " Search Apps...")
                           : (String(LV_SYMBOL_EYE_OPEN " ") + s_exploreQuery);
    lv_label_set_text(searchLbl, searchTxt.c_str());
    lv_obj_set_width(searchLbl, LV_PCT(100));
    lv_label_set_long_mode(searchLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(searchLbl, lv_color_hex(s_exploreQuery.isEmpty() ? m3TextSecondaryHex() : m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(searchLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(searchLbl, LV_ALIGN_LEFT_MID, 0, 0);

    if (!s_exploreQuery.isEmpty()) {
        lv_obj_t* clearBtn = m3PillBtn(searchRow, LV_SYMBOL_CLOSE, m3SurfaceHighestHex(), m3TextPrimaryHex(), 26, 28);
        lv_obj_add_event_cb(clearBtn, exploreClearSearchCb, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* urlBtn = m3PillBtn(searchRow, LV_SYMBOL_PLUS " URL", M3Color::PRIMARY_CONT, M3Color::PRIMARY, 54, 28);
    lv_obj_add_event_cb(urlBtn, exploreUrlBtnCb, LV_EVENT_CLICKED, nullptr);

    // 3. Sort chips row (Popular | Recent | A-Z | Starred)
    lv_obj_t* sortRow = lv_obj_create(container);
    lv_obj_set_size(sortRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sortRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sortRow, 0, 0);
    lv_obj_set_style_pad_all(sortRow, 0, 0);
    lv_obj_set_style_pad_column(sortRow, 4, 0);
    lv_obj_set_flex_flow(sortRow, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(sortRow, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < static_cast<uint8_t>(AppStoreSort::Count); ++i) {
        AppStoreSort s = static_cast<AppStoreSort>(i);
        bool active = (s == s_exploreSort);
        createCompactChip(sortRow,
                          appStoreSortLabel(s),
                          active ? M3Color::PRIMARY_CONT : m3SurfaceHighestHex(),
                          active ? M3Color::PRIMARY : m3TextSecondaryHex(),
                          exploreSortChipCb,
                          reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }

    // 4. Auto-fetch on first visit
    if (!s_exploreLoaded && !s_exploreLoading && s_exploreError.isEmpty()) {
        lv_obj_t* loadCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_flex_flow(loadCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(loadCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(loadCard, 12, 0);

        lv_obj_t* lbl = lv_label_create(loadCard);
        lv_label_set_text(lbl, "Connecting to LauncherHub...");
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

        queueExploreAction(PendingExploreAction::FetchCatalog);
        return;
    }

    // 5. Error state
    if (!s_exploreError.isEmpty()) {
        lv_obj_t* errCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_flex_flow(errCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(errCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(errCard, 10, 0);
        lv_obj_set_style_pad_row(errCard, 6, 0);

        lv_obj_t* errLbl = lv_label_create(errCard);
        lv_label_set_text(errLbl, s_exploreError.c_str());
        lv_obj_set_width(errLbl, LV_PCT(100));
        lv_label_set_long_mode(errLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(errLbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(errLbl, lv_color_hex(M3Color::CORAL), 0);
        lv_obj_set_style_text_font(errLbl, &lv_font_montserrat_12, 0);

        lv_obj_t* retryBtn = m3PillBtn(errCard, LV_SYMBOL_REFRESH "  Retry",
                                       M3Color::PRIMARY_CONT, M3Color::PRIMARY, 110, 28);
        lv_obj_add_event_cb(retryBtn, exploreRetryBtnCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    // 6. Empty state
    if (s_exploreItems.empty()) {
        lv_obj_t* emptyCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_flex_flow(emptyCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(emptyCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(emptyCard, 12, 0);
        lv_obj_set_style_pad_row(emptyCard, 6, 0);

        lv_obj_t* eLbl = lv_label_create(emptyCard);
        lv_label_set_text(eLbl, s_exploreQuery.isEmpty() ? "No Apps found for this filter."
                                                         : "No Apps match your search.");
        lv_obj_set_width(eLbl, LV_PCT(100));
        lv_label_set_long_mode(eLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(eLbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(eLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(eLbl, &lv_font_montserrat_12, 0);

        lv_obj_t* actBtn = m3PillBtn(emptyCard,
                                     s_exploreQuery.isEmpty() ? LV_SYMBOL_REFRESH "  Refresh"
                                                              : LV_SYMBOL_CLOSE "  Clear Search",
                                     M3Color::PRIMARY_CONT, M3Color::PRIMARY, 120, 28);
        lv_obj_add_event_cb(actBtn,
                            s_exploreQuery.isEmpty() ? exploreRetryBtnCb : exploreClearSearchCb,
                            LV_EVENT_CLICKED, nullptr);
        return;
    }

    // 7. Material 3 App Cards
    for (size_t i = 0; i < s_exploreItems.size(); ++i) {
        const ExploreCatalogItem& item = s_exploreItems[i];
        uint32_t accentHex = M3Color::PRIMARY;
        uint32_t containerHex = 0x2563EB;
        launcherHomePickColors(item.fid.c_str(), &accentHex, &containerHex);

        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_pad_column(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, exploreItemCardCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        lv_obj_t* badge = lv_obj_create(card);
        lv_obj_set_size(badge, 28, 28);
        lv_obj_set_style_radius(badge, 9, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(containerHex), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* badgeIco = lv_label_create(badge);
        lv_label_set_text(badgeIco, item.starred ? LV_SYMBOL_OK : LV_SYMBOL_DOWNLOAD);
        lv_obj_set_style_text_color(badgeIco, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(badgeIco, &lv_font_montserrat_12, 0);
        lv_obj_center(badgeIco);

        lv_obj_t* textCol = lv_obj_create(card);
        lv_obj_set_height(textCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textCol, 1);
        lv_obj_set_style_bg_opa(textCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textCol, 0, 0);
        lv_obj_set_style_pad_all(textCol, 0, 0);
        lv_obj_set_style_pad_row(textCol, 1, 0);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* nameLbl = lv_label_create(textCol);
        lv_label_set_text(nameLbl, item.name.c_str());
        lv_obj_set_width(nameLbl, LV_PCT(100));
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_14, 0);

        String subTxt = String("by ") + item.author;
        if (item.starred) subTxt += "  •  Featured";
        lv_obj_t* subLbl = lv_label_create(textCol);
        lv_label_set_text(subLbl, subTxt.c_str());
        lv_obj_set_width(subLbl, LV_PCT(100));
        lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(subLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* chev = lv_label_create(card);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_10, 0);
    }

    // 8. Pagination bar
    lv_obj_t* pageRow = lv_obj_create(container);
    lv_obj_set_size(pageRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pageRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pageRow, 0, 0);
    lv_obj_set_style_pad_all(pageRow, 0, 0);
    lv_obj_set_style_pad_top(pageRow, 2, 0);
    lv_obj_set_flex_flow(pageRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pageRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pageRow, LV_OBJ_FLAG_SCROLLABLE);

    bool canPrev = s_explorePage > 1;
    bool canNext = static_cast<int>(s_explorePage) < s_exploreNumPages;

    lv_obj_t* prevBtn = m3PillBtn(pageRow, LV_SYMBOL_LEFT " Prev",
                                  canPrev ? M3Color::PRIMARY_CONT : m3SurfaceHighestHex(),
                                  canPrev ? M3Color::PRIMARY : m3TextSecondaryHex(), 58, 26);
    if (canPrev) {
        lv_obj_add_event_cb(prevBtn, explorePageBtnCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    } else {
        lv_obj_set_style_opa(prevBtn, LV_OPA_50, 0);
    }

    char pageBuf[32];
    appStoreFormatPageLabel(s_explorePage, s_exploreNumPages, s_exploreTotal, pageBuf, sizeof(pageBuf));
    lv_obj_t* pageLbl = lv_label_create(pageRow);
    lv_label_set_text(pageLbl, pageBuf);
    lv_obj_set_style_text_color(pageLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(pageLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* nextBtn = m3PillBtn(pageRow, "Next " LV_SYMBOL_RIGHT,
                                  canNext ? M3Color::PRIMARY_CONT : m3SurfaceHighestHex(),
                                  canNext ? M3Color::PRIMARY : m3TextSecondaryHex(), 58, 26);
    if (canNext) {
        lv_obj_add_event_cb(nextBtn, explorePageBtnCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
    } else {
        lv_obj_set_style_opa(nextBtn, LV_OPA_50, 0);
    }
}

// --- SD Card Tab Logic (/downloads/ & SD Browser, Narrated Install, File Ops) ---

void resolveDefaultSdPath() {
    if (!s_sdPathInitialized || s_sdCurrentPath.isEmpty()) {
        s_sdCurrentPath = SD.exists("/downloads") ? String("/downloads") : String("/");
        s_sdPathInitialized = true;
    } else if (s_sdCurrentPath != "/" && !SD.exists(s_sdCurrentPath)) {
        s_sdCurrentPath = SD.exists("/downloads") ? String("/downloads") : String("/");
    }
}

void promptSdNewFolder() {
    openTextPromptModal("New Folder", "Folder name", "", 48, "Create",
                        [](const char* text) {
                            String name(text ? text : "");
                            name.trim();
                            if (!appStoreIsValidSdName(name.c_str())) {
                                launcherUiError("Enter a valid folder name (no / \\ : * ? \" < > |).", "Invalid Name");
                                return;
                            }
                            if (createFolderOnSd(s_sdCurrentPath, name)) {
                                m3ShowToast("Folder created", 1200);
                            } else {
                                launcherUiError("Could not create folder on SD card.", "New Folder");
                            }
                            if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
                        });
}

void promptSdRenameEntry(const LauncherSdEntry& entry) {
    String fullPath = entry.fullPath;
    openTextPromptModal("Rename Item", "New name", entry.name.c_str(), 48, "Rename",
                        [fullPath](const char* text) {
                            String name(text ? text : "");
                            name.trim();
                            if (!appStoreIsValidSdName(name.c_str())) {
                                launcherUiError("Enter a valid file or folder name.", "Invalid Name");
                                return;
                            }
                            if (renameOnSd(fullPath, name)) {
                                m3ShowToast("Renamed", 1200);
                            } else {
                                launcherUiError("Could not rename item on SD card.", "Rename");
                            }
                            if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
                        });
}

void runSdDeleteEntry(const LauncherSdEntry& entry) {
    String msg = String("Permanently delete \"") + entry.name + "\"";
    if (entry.isDir) msg += " and all files inside it";
    msg += " from SD card?";
    if (!launcherUiConfirm("Delete Item", msg.c_str(), "Delete", M3Color::CORAL)) {
        return;
    }
    if (deleteFromSd(entry.fullPath)) {
        if (hasFileToCopy() && getFileToCopy() == entry.fullPath) {
            clearFileToCopy();
        }
        m3ShowToast("Deleted", 1200);
    } else {
        launcherUiError("Could not delete item from SD card.", "Delete Failed");
    }
    if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
}

void runSdEntryMenu(const LauncherSdEntry& entry) {
    bool isBin = !entry.isDir && appStoreIsBinFile(entry.name.c_str());
    std::vector<LauncherUiOption> opts;

    if (isBin) {
        opts.push_back({String(LV_SYMBOL_DOWNLOAD "  Install App"), [entry]() {
            updateFromSD(entry.fullPath, true, false);
        }});
    }
    if (!entry.isDir) {
        opts.push_back({String(LV_SYMBOL_COPY "  Copy File"), [entry]() {
            if (copyFile(entry.fullPath)) {
                m3ShowToast("Copied to clipboard", 1200);
                if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
            }
        }});
    }
    opts.push_back({String(LV_SYMBOL_EDIT "  Rename"), [entry]() {
        promptSdRenameEntry(entry);
    }});
    opts.push_back({String(LV_SYMBOL_TRASH "  Delete"), [entry]() {
        runSdDeleteEntry(entry);
    }});

    String subtitle = entry.isDir ? String("Folder") : entry.fullPath;
    launcherUiSelectOption(entry.name.c_str(), opts, subtitle.c_str());
}

void asyncDispatchSdAction(void* arg) {
    (void)arg;
    PendingSdAction action = s_pendingSdAction;
    size_t idx = s_pendingSdIdx;
    s_pendingSdAction = PendingSdAction::None;
    s_pendingSdIdx = 0;

    switch (action) {
        case PendingSdAction::InstallBin:
            if (idx < s_sdEntries.size()) {
                LauncherSdEntry entry = s_sdEntries[idx];
                updateFromSD(entry.fullPath, true, false);
            }
            break;
        case PendingSdAction::EntryMenu:
            if (idx < s_sdEntries.size()) {
                LauncherSdEntry entry = s_sdEntries[idx];
                runSdEntryMenu(entry);
            }
            break;
        case PendingSdAction::DeleteEntry:
            if (idx < s_sdEntries.size()) {
                LauncherSdEntry entry = s_sdEntries[idx];
                runSdDeleteEntry(entry);
            }
            break;
        case PendingSdAction::RenameEntry:
            if (idx < s_sdEntries.size()) {
                LauncherSdEntry entry = s_sdEntries[idx];
                promptSdRenameEntry(entry);
            }
            break;
        case PendingSdAction::NewFolder:
            promptSdNewFolder();
            break;
        case PendingSdAction::CopyEntry:
            if (idx < s_sdEntries.size()) {
                if (copyFile(s_sdEntries[idx].fullPath)) {
                    m3ShowToast("Copied to clipboard", 1200);
                    if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
                }
            }
            break;
        case PendingSdAction::PasteHere:
            if (hasFileToCopy()) {
                pasteFile(s_sdCurrentPath);
                if (s_storeBody && s_activeTab == AppStoreTab::SdCard) renderActiveTab();
            }
            break;
        default:
            break;
    }
}

void queueSdAction(PendingSdAction action, size_t entryIdx) {
    s_pendingSdAction = action;
    s_pendingSdIdx = entryIdx;
    lv_async_call(asyncDispatchSdAction, nullptr);
}

void sdUpBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    std::string parent = appStoreParentPath(s_sdCurrentPath.c_str());
    if (s_sdCurrentPath != parent.c_str()) {
        s_sdCurrentPath = String(parent.c_str());
        renderActiveTab();
    }
}

void sdQuickJumpBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (s_sdCurrentPath == "/downloads") {
        s_sdCurrentPath = "/";
    } else {
        if (!SD.exists("/downloads")) {
            SD.mkdir("/downloads");
        }
        s_sdCurrentPath = "/downloads";
    }
    renderActiveTab();
}

void sdRefreshBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    setupSdCard();
    renderActiveTab();
}

void sdToggleFilterCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_sdOnlyBins = !s_sdOnlyBins;
    renderActiveTab();
}

void sdNewFolderBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueSdAction(PendingSdAction::NewFolder);
}

void sdPasteBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueSdAction(PendingSdAction::PasteHere);
}

void sdClearCopyBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    clearFileToCopy();
    renderActiveTab();
}

void sdOpenFolderCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (idx >= s_sdEntries.size() || !s_sdEntries[idx].isDir) return;
    s_sdCurrentPath = String(appStoreNormalizeSdPath(s_sdEntries[idx].fullPath.c_str()).c_str());
    renderActiveTab();
}

void sdInstallBinBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (idx >= s_sdEntries.size()) return;
    queueSdAction(PendingSdAction::InstallBin, idx);
}

void sdEntryMenuBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (idx >= s_sdEntries.size()) return;
    queueSdAction(PendingSdAction::EntryMenu, idx);
}

void buildSdCardTab(lv_obj_t* container) {
    // Attempt mount automatically if not currently mounted
    if (!isSdCardMounted()) {
        setupSdCard();
    }

    // 1. Unmounted state
    if (!isSdCardMounted()) {
        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 6, 0);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, LV_SYMBOL_SD_CARD);
        lv_obj_set_style_text_color(ico, lv_color_hex(m3PrimaryHex()), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, "No SD Card Detected");
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

        lv_obj_t* sub = lv_label_create(card);
        lv_label_set_text(sub, "Insert a FAT32 microSD card with .bin Apps or backups, then tap Mount.");
        lv_obj_set_width(sub, LV_PCT(100));
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);

        lv_obj_t* mountBtn = m3PillBtn(card, LV_SYMBOL_REFRESH "  Mount SD Card",
                                       M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 32);
        lv_obj_add_event_cb(mountBtn, checkSdBtnCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* exploreBtn = m3PillBtn(card, LV_SYMBOL_WIFI "  Browse Explore Instead",
                                         m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 28);
        lv_obj_add_event_cb(exploreBtn, switchTabBtnCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(AppStoreTab::Explore)));
        return;
    }

    resolveDefaultSdPath();
    bool listed = listSdDirectory(s_sdCurrentPath, s_sdEntries, s_sdOnlyBins, true);
    if (!listed && s_sdCurrentPath != "/") {
        s_sdCurrentPath = "/";
        listed = listSdDirectory(s_sdCurrentPath, s_sdEntries, s_sdOnlyBins, true);
    }
    if (!listed) {
        // Card was removed mid-session; re-render unmounted state
        unmountSdCard();
        lv_obj_t* errCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_flex_flow(errCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(errCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(errCard, 10, 0);
        lv_obj_set_style_pad_row(errCard, 6, 0);

        lv_obj_t* lbl = lv_label_create(errCard);
        lv_label_set_text(lbl, "SD card could not be read.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::CORAL), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

        lv_obj_t* retryBtn = m3PillBtn(errCard, LV_SYMBOL_REFRESH "  Retry",
                                       M3Color::PRIMARY_CONT, M3Color::PRIMARY, 110, 28);
        lv_obj_add_event_cb(retryBtn, checkSdBtnCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    // 2. Path navigation bar (Up | Path | Quick-jump | Refresh)
    lv_obj_t* pathRow = lv_obj_create(container);
    lv_obj_set_size(pathRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pathRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pathRow, 0, 0);
    lv_obj_set_style_pad_all(pathRow, 0, 0);
    lv_obj_set_style_pad_column(pathRow, 4, 0);
    lv_obj_set_flex_flow(pathRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pathRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pathRow, LV_OBJ_FLAG_SCROLLABLE);

    bool atRoot = (s_sdCurrentPath == "/");
    lv_obj_t* upBtn = m3PillBtn(pathRow, LV_SYMBOL_UP,
                                atRoot ? m3SurfaceHighestHex() : M3Color::PRIMARY_CONT,
                                atRoot ? m3TextSecondaryHex() : M3Color::PRIMARY, 28, 26);
    if (!atRoot) {
        lv_obj_add_event_cb(upBtn, sdUpBtnCb, LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_set_style_opa(upBtn, LV_OPA_50, 0);
    }

    lv_obj_t* pathPill = lv_obj_create(pathRow);
    lv_obj_set_height(pathPill, 26);
    lv_obj_set_flex_grow(pathPill, 1);
    lv_obj_set_style_bg_color(pathPill, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(pathPill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pathPill, 1, 0);
    lv_obj_set_style_border_color(pathPill, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_border_opa(pathPill, LV_OPA_50, 0);
    lv_obj_set_style_radius(pathPill, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_hor(pathPill, 8, 0);
    lv_obj_set_style_pad_ver(pathPill, 0, 0);
    lv_obj_clear_flag(pathPill, LV_OBJ_FLAG_SCROLLABLE);

    char pathDisp[32];
    appStoreFormatSdPathDisplay(s_sdCurrentPath.c_str(), pathDisp, sizeof(pathDisp));
    lv_obj_t* pathLbl = lv_label_create(pathPill);
    lv_label_set_text(pathLbl, pathDisp);
    lv_obj_set_width(pathLbl, LV_PCT(100));
    lv_label_set_long_mode(pathLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(pathLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(pathLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(pathLbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* refBtn = m3PillBtn(pathRow, LV_SYMBOL_REFRESH,
                                 m3SurfaceHighestHex(), m3TextPrimaryHex(), 28, 26);
    lv_obj_add_event_cb(refBtn, sdRefreshBtnCb, LV_EVENT_CLICKED, nullptr);

    // 3. Action chips row (+ Folder | /downloads or Root | .bin Filter)
    lv_obj_t* toolRow = lv_obj_create(container);
    lv_obj_set_size(toolRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(toolRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(toolRow, 0, 0);
    lv_obj_set_style_pad_all(toolRow, 0, 0);
    lv_obj_set_style_pad_column(toolRow, 4, 0);
    lv_obj_set_flex_flow(toolRow, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(toolRow, LV_OBJ_FLAG_SCROLLABLE);

    createCompactChip(toolRow, LV_SYMBOL_PLUS " Folder",
                      m3SurfaceHighestHex(), m3TextPrimaryHex(),
                      sdNewFolderBtnCb, nullptr);

    bool inDownloads = (s_sdCurrentPath == "/downloads");
    createCompactChip(toolRow, inDownloads ? LV_SYMBOL_SD_CARD " Root" : LV_SYMBOL_DOWNLOAD " Downloads",
                      M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                      sdQuickJumpBtnCb, nullptr);

    createCompactChip(toolRow, s_sdOnlyBins ? LV_SYMBOL_OK " .bin" : "All Files",
                      s_sdOnlyBins ? M3Color::PRIMARY_CONT : m3SurfaceHighestHex(),
                      s_sdOnlyBins ? M3Color::PRIMARY : m3TextSecondaryHex(),
                      sdToggleFilterCb, nullptr);

    // 4. Active clipboard banner (when a file is staged for Copy/Paste)
    if (hasFileToCopy()) {
        String clipPath = getFileToCopy();
        String clipName = clipPath.substring(clipPath.lastIndexOf('/') + 1);

        lv_obj_t* clipCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 12);
        lv_obj_set_style_pad_all(clipCard, 6, 0);
        lv_obj_set_style_pad_column(clipCard, 5, 0);
        lv_obj_set_flex_flow(clipCard, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(clipCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* clipLbl = lv_label_create(clipCard);
        lv_label_set_text(clipLbl, (String(LV_SYMBOL_COPY " ") + clipName).c_str());
        lv_obj_set_flex_grow(clipLbl, 1);
        lv_label_set_long_mode(clipLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(clipLbl, lv_color_hex(m3PrimaryHex()), 0);
        lv_obj_set_style_text_font(clipLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* pasteBtn = m3PillBtn(clipCard, "Paste", M3Color::PRIMARY, M3Color::ON_PRIMARY, 48, 24);
        lv_obj_add_event_cb(pasteBtn, sdPasteBtnCb, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* clrBtn = m3PillBtn(clipCard, LV_SYMBOL_CLOSE, m3SurfaceHighestHex(), m3TextSecondaryHex(), 24, 24);
        lv_obj_add_event_cb(clrBtn, sdClearCopyBtnCb, LV_EVENT_CLICKED, nullptr);
    }

    // 5. Empty folder state
    if (s_sdEntries.empty()) {
        lv_obj_t* emptyCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_flex_flow(emptyCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(emptyCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(emptyCard, 12, 0);
        lv_obj_set_style_pad_row(emptyCard, 6, 0);

        lv_obj_t* ico = lv_label_create(emptyCard);
        lv_label_set_text(ico, LV_SYMBOL_DIRECTORY);
        lv_obj_set_style_text_color(ico, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

        lv_obj_t* eTitle = lv_label_create(emptyCard);
        lv_label_set_text(eTitle, s_sdOnlyBins ? "No .bin Apps Here" : "Folder is Empty");
        lv_obj_set_style_text_color(eTitle, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(eTitle, &lv_font_montserrat_14, 0);

        lv_obj_t* eDesc = lv_label_create(emptyCard);
        lv_label_set_text(eDesc, s_sdOnlyBins
                                     ? "Switch to All Files or download Apps from Explore to /downloads/."
                                     : "Copy .bin Apps to your SD card or download them from Explore.");
        lv_obj_set_width(eDesc, LV_PCT(100));
        lv_label_set_long_mode(eDesc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(eDesc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(eDesc, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(eDesc, &lv_font_montserrat_10, 0);

        if (s_sdOnlyBins) {
            lv_obj_t* allBtn = m3PillBtn(emptyCard, "Show All Files",
                                         M3Color::PRIMARY_CONT, M3Color::PRIMARY, 124, 28);
            lv_obj_add_event_cb(allBtn, sdToggleFilterCb, LV_EVENT_CLICKED, nullptr);
        } else {
            lv_obj_t* expBtn = m3PillBtn(emptyCard, LV_SYMBOL_WIFI "  Go to Explore",
                                         M3Color::PRIMARY_CONT, M3Color::PRIMARY, 132, 28);
            lv_obj_add_event_cb(expBtn, switchTabBtnCb, LV_EVENT_CLICKED,
                                reinterpret_cast<void*>(static_cast<intptr_t>(AppStoreTab::Explore)));
        }
        return;
    }

    // 6. Folder & File cards
    for (size_t i = 0; i < s_sdEntries.size(); ++i) {
        const LauncherSdEntry& entry = s_sdEntries[i];
        bool isBin = !entry.isDir && appStoreIsBinFile(entry.name.c_str());

        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 13);
        lv_obj_set_style_pad_all(card, 7, 0);
        lv_obj_set_style_pad_column(card, 7, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, entry.isDir ? sdOpenFolderCb : sdEntryMenuBtnCb,
                            LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        // Badge
        uint32_t badgeBg = m3SurfaceHighestHex();
        uint32_t badgeFg = m3TextSecondaryHex();
        const char* badgeSym = LV_SYMBOL_FILE;
        if (entry.isDir) {
            badgeBg = 0x3D2E14;
            badgeFg = M3Color::AMBER;
            badgeSym = LV_SYMBOL_DIRECTORY;
        } else if (isBin) {
            uint32_t accentHex = M3Color::PRIMARY;
            uint32_t containerHex = 0x2563EB;
            launcherHomePickColors(entry.name.c_str(), &accentHex, &containerHex);
            badgeBg = containerHex;
            badgeFg = 0xFFFFFF;
            badgeSym = LV_SYMBOL_DOWNLOAD;
        }

        lv_obj_t* badge = lv_obj_create(card);
        lv_obj_set_size(badge, 26, 26);
        lv_obj_set_style_radius(badge, 8, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(badgeBg), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* badgeIco = lv_label_create(badge);
        lv_label_set_text(badgeIco, badgeSym);
        lv_obj_set_style_text_color(badgeIco, lv_color_hex(badgeFg), 0);
        lv_obj_set_style_text_font(badgeIco, &lv_font_montserrat_12, 0);
        lv_obj_center(badgeIco);

        // Name + subtitle
        lv_obj_t* textCol = lv_obj_create(card);
        lv_obj_set_height(textCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textCol, 1);
        lv_obj_set_style_bg_opa(textCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textCol, 0, 0);
        lv_obj_set_style_pad_all(textCol, 0, 0);
        lv_obj_set_style_pad_row(textCol, 1, 0);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* nameLbl = lv_label_create(textCol);
        lv_label_set_text(nameLbl, entry.name.c_str());
        lv_obj_set_width(nameLbl, LV_PCT(100));
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);

        char subBuf[40];
        if (entry.isDir) {
            snprintf(subBuf, sizeof(subBuf), "Folder");
        } else {
            char szBuf[20];
            appStoreFormatSizeCompact(static_cast<uint32_t>(entry.size), szBuf, sizeof(szBuf));
            if (isBin) {
                snprintf(subBuf, sizeof(subBuf), "%s • App Binary", szBuf);
            } else {
                snprintf(subBuf, sizeof(subBuf), "%s", szBuf);
            }
        }
        lv_obj_t* subLbl = lv_label_create(textCol);
        lv_label_set_text(subLbl, subBuf);
        lv_obj_set_width(subLbl, LV_PCT(100));
        lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(subLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_10, 0);

        // Quick action buttons
        if (isBin) {
            lv_obj_t* instBtn = m3PillBtn(card, "Install", M3Color::PRIMARY, M3Color::ON_PRIMARY, 48, 24);
            lv_obj_add_event_cb(instBtn, sdInstallBinBtnCb, LV_EVENT_CLICKED,
                                reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        }

        lv_obj_t* moreBtn = m3PillBtn(card, LV_SYMBOL_EDIT, m3SurfaceHighestHex(), m3TextSecondaryHex(), 24, 24);
        lv_obj_add_event_cb(moreBtn, sdEntryMenuBtnCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
}

// ============================================================================
// Tools Tab (Storage & Partitions / PMan, Backup & Restore, Check for Updates)
// ============================================================================

struct FlashBarSegment {
    uint32_t offset;
    uint32_t size;
    uint32_t colorHex;
    FlashBarSegment(uint32_t o = 0, uint32_t s = 0, uint32_t c = 0x4285F4)
        : offset(o), size(s), colorHex(c) {}
};

void buildFlashMapBar(lv_obj_t* parent, const LauncherPartitionTable& table) {
    if (table.flashSize == 0) return;

    std::vector<FlashBarSegment> segs;
    for (const LauncherPartitionEntry& e : table.entries) {
        if (e.size == 0) continue;
        segs.push_back({e.offset, e.size, appStorePartitionColorHex(e.type, e.subtype)});
    }
    std::vector<LauncherPartitionRange> freeRanges = launcherPartitionFreeRanges(table);
    uint32_t freeHex = m3SurfaceHighestHex();
    for (const LauncherPartitionRange& r : freeRanges) {
        if (r.size == 0) continue;
        segs.push_back({r.offset, r.size, freeHex});
    }
    std::sort(segs.begin(), segs.end(), [](const FlashBarSegment& a, const FlashBarSegment& b) {
        return a.offset < b.offset;
    });

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 14);
    lv_obj_set_style_bg_color(bar, lv_color_hex(freeHex), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 1, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_clip_corner(bar, true, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (const FlashBarSegment& seg : segs) {
        uint8_t pct = appStoreComputeSegmentWidthPct(seg.size, table.flashSize);
        if (pct == 0) continue;
        lv_obj_t* part = lv_obj_create(bar);
        lv_obj_set_height(part, LV_PCT(100));
        lv_obj_set_flex_grow(part, pct);
        lv_obj_set_style_min_width(part, 3, 0);
        lv_obj_set_style_bg_color(part, lv_color_hex(seg.colorHex), 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_style_radius(part, 0, 0);
        lv_obj_set_style_pad_all(part, 0, 0);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Compact legend row
    lv_obj_t* leg = lv_obj_create(parent);
    lv_obj_set_size(leg, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leg, 0, 0);
    lv_obj_set_style_pad_all(leg, 0, 0);
    lv_obj_set_style_pad_column(leg, 8, 0);
    lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(leg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(leg, LV_OBJ_FLAG_SCROLLABLE);

    const struct { const char* name; uint32_t hex; } items[4] = {
        {"Sys",  0x4285F4u},
        {"App",  0x34A853u},
        {"Data", 0xFBBC04u},
        {"Free", freeHex}
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* item = lv_obj_create(leg);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_pad_column(item, 3, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* dot = lv_obj_create(item);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(items[i].hex), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(item);
        lv_label_set_text(lbl, items[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    }
}

bool ensureBackupConfigForToolsRow(const ToolsBackupRow& row, String& outAppNum) {
    outAppNum = row.appNum;
    if (outAppNum.isEmpty()) {
        outAppNum = launcherGenerateAppNum(String("app:") + row.appLabel);
    }

    BackupInstallInfo existing = loadInstalledFromConfig(outAppNum);
    if (!existing.appNum.isEmpty() && !existing.partitions.empty()) {
        return true;
    }

    LauncherPartitionTable table;
    launcherPartitionReadCurrent(table);

    BackupInstallInfo info;
    info.appNum = outAppNum;
    info.appName = row.appName;
    info.sdFilepath = existing.sdFilepath;

    for (const String& fatLbl : row.fatLabels) {
        if (fatLbl.isEmpty()) continue;
        BackupPartitionInfo p;
        p.type = "FAT";
        p.label = fatLbl;
        info.partitions.push_back(p);
    }
    if (!row.spiffsLabel.isEmpty()) {
        BackupPartitionInfo p;
        p.label = row.spiffsLabel;
        p.type = "SPIFFS";
        const LauncherPartitionEntry* entry = launcherPartitionFindByLabel(table, row.spiffsLabel.c_str());
        if (entry && entry->subtype == 0x83) p.type = "LittleFS";
        info.partitions.push_back(p);
    }
    if (info.partitions.empty()) return false;
    return saveInstalledToConfig(info);
}

std::vector<ToolsBackupRow> collectToolsBackupRows() {
    std::vector<ToolsBackupRow> rows;
    std::vector<InstalledRowData> installed = collectInstalledRows(nullptr);

    for (const InstalledRowData& inst : installed) {
        String num = inst.appNum;
        if (num.isEmpty()) num = launcherGenerateAppNum(String("app:") + inst.label);
        BackupInstallInfo bkp = loadInstalledFromConfig(num);
        bool hasBkp = hasRestorableBackup(bkp);
        uint8_t partCount = inst.dataPartCount > 0
                                ? inst.dataPartCount
                                : static_cast<uint8_t>(bkp.partitions.size());
        if (partCount == 0 && !hasBkp) continue;

        ToolsBackupRow r;
        r.appNum = num;
        r.appName = inst.name;
        r.appLabel = inst.label;
        r.dataPartCount = partCount;
        r.isInstalled = true;
        r.hasBackup = hasBkp;
        r.fatLabels = inst.fatLabels;
        r.spiffsLabel = inst.spiffsLabel;
        rows.push_back(r);
    }

    // Append any archived backup records from /bkp/backupData.json not currently installed
    std::vector<BackupInstallInfo> saved = listAllBackupEntries();
    for (const BackupInstallInfo& entry : saved) {
        bool seen = false;
        for (const ToolsBackupRow& existing : rows) {
            if (existing.appNum == entry.appNum) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        ToolsBackupRow r;
        r.appNum = entry.appNum;
        r.appName = entry.appName.isEmpty() ? entry.appNum : entry.appName;
        r.dataPartCount = static_cast<uint8_t>(entry.partitions.size());
        r.isInstalled = false;
        r.hasBackup = hasRestorableBackup(entry);
        rows.push_back(r);
    }
    return rows;
}

void pickRestoreBinAndFlash(const LauncherPartitionEntry& entry) {
    if (!setupSdCard()) {
        launcherUiError("Insert a FAT32 SD card to restore a partition.", "SD Card Required");
        return;
    }

    std::vector<LauncherSdEntry> candidates;
    const char* searchDirs[2] = {"/dumps", "/downloads"};
    for (int d = 0; d < 2; ++d) {
        if (!SD.exists(searchDirs[d])) continue;
        std::vector<LauncherSdEntry> list;
        if (listSdDirectory(searchDirs[d], list, true, true)) {
            for (const LauncherSdEntry& item : list) {
                if (!item.isDir && appStoreIsBinFile(item.name.c_str())) {
                    candidates.push_back(item);
                }
            }
        }
    }

    if (candidates.empty()) {
        launcherUiInfo("No .bin files found in /dumps or /downloads on SD card.", "Restore Partition");
        return;
    }

    std::vector<LauncherUiOption> opts;
    for (const LauncherSdEntry& c : candidates) {
        char szBuf[20];
        appStoreFormatSizeCompact(static_cast<uint32_t>(c.size), szBuf, sizeof(szBuf));
        String label = c.name + " (" + szBuf + ")";
        String fullPath = c.fullPath;
        opts.push_back({label, [entry, fullPath]() {
            launcherRestorePartitionFromSd(entry, fullPath.c_str());
        }});
    }
    String sub = String("Restore into ") + entry.label;
    launcherUiSelectOption("Select .bin File", opts, sub.c_str());
}

void runPartitionMenu(const LauncherPartitionEntry& entry) {
    char metaBuf[64];
    appStoreFormatPartitionMeta(entry.offset, entry.size, entry.type, entry.subtype, metaBuf, sizeof(metaBuf));

    std::vector<LauncherUiOption> opts;
    opts.push_back({String(LV_SYMBOL_SAVE "  Dump to SD (/dumps)"), [entry]() {
        launcherDumpPartitionToSd(entry);
    }});

    bool protectedPart = appStoreIsProtectedPartition(entry.type, entry.subtype);
    if (!protectedPart) {
        opts.push_back({String(LV_SYMBOL_UPLOAD "  Restore from SD (.bin)"), [entry]() {
            pickRestoreBinAndFlash(entry);
        }});
    }

    if (entry.isOtaApp()) {
        String lbl(entry.label);
        opts.push_back({String(LV_SYMBOL_PLAY "  Launch App"), [lbl]() {
            launcherBootAppByLabel(lbl.c_str());
        }});
        opts.push_back({String(LV_SYMBOL_TRASH "  Uninstall App"), [lbl]() {
            launcherDeleteAppByLabel(lbl.c_str(), false, false);
        }});
    } else if (launcherPartitionIsRemovableInstallData(entry)) {
        opts.push_back({String(LV_SYMBOL_TRASH "  Delete Data Partition"), [entry]() {
            launcherDeleteDataPartition(entry);
        }});
    }

    String title = String(entry.label[0] ? entry.label : "Partition");
    launcherUiSelectOption(title.c_str(), opts, metaBuf);
}

void runCheckForAppUpdates() {
    launcherUiBeginProgress("Check Updates", "Contacting LauncherHub...", 0, false);
    String err;
    bool ok = checkForUpdates(&err);
    if (!ok) {
        if (err.indexOf("up to date") >= 0 || err.indexOf("No SD downloads") >= 0) {
            launcherUiFinish(true, "Up to Date", err.c_str());
        } else {
            launcherUiError(err.isEmpty() ? String("Could not check for updates.") : err, "Update Check");
        }
        return;
    }

    // Copy updated items into Explore catalog so the user can tap & install right away.
    s_exploreItems.clear();
    JsonArray items = g_launcherHubDoc["items"].as<JsonArray>();
    if (!items.isNull()) {
        for (JsonObject obj : items) {
            ExploreCatalogItem it;
            it.fid = obj["fid"].as<String>();
            it.name = obj["name"].as<String>();
            it.author = obj["author"].as<String>();
            it.version = obj["version"].as<String>();
            it.file = obj["file"].as<String>();
            it.starred = obj["star"].as<bool>();
            if (!it.fid.isEmpty() && !it.name.isEmpty()) {
                s_exploreItems.push_back(it);
            }
        }
    }
    g_launcherHubDoc.clear();

    s_exploreTotal = static_cast<int>(s_exploreItems.size());
    s_exploreNumPages = 1;
    s_explorePage = 1;
    s_exploreLoaded = true;
    s_exploreLoading = false;
    s_exploreError = "";
    s_exploreQuery = "";
    s_exploreDetail.active = false;

    launcherUiClose();
    s_activeTab = AppStoreTab::Explore;
    updateTabBarStyle();
    renderActiveTab();

    char toastBuf[48];
    snprintf(toastBuf, sizeof(toastBuf), "Found %d app %s!",
             s_exploreTotal, (s_exploreTotal == 1) ? "update" : "updates");
    m3ShowToast(toastBuf, 2200);
}

void asyncDispatchToolsAction(void* arg) {
    (void)arg;
    PendingToolsAction action = s_pendingToolsAction;
    size_t idx = s_pendingToolsIdx;
    s_pendingToolsAction = PendingToolsAction::None;
    s_pendingToolsIdx = 0;

    if (action == PendingToolsAction::None) return;

    switch (action) {
        case PendingToolsAction::CompactFlash:
            launcherCompactFlashPartitions();
            if (s_storeBody) renderActiveTab();
            break;
        case PendingToolsAction::DumpTable:
            launcherDumpPartitionTableToSd();
            break;
        case PendingToolsAction::PartitionMenu:
            if (idx < s_pmanEntries.size()) {
                LauncherPartitionEntry entry = s_pmanEntries[idx];
                runPartitionMenu(entry);
                if (s_storeBody) renderActiveTab();
            }
            break;
        case PendingToolsAction::BackupAllApps: {
            if (!setupSdCard()) {
                launcherUiError("Insert a FAT32 SD card to back up App data.", "SD Card Required");
                break;
            }
            int backedUp = 0;
            for (const ToolsBackupRow& row : s_backupRows) {
                if (!row.isInstalled || row.dataPartCount == 0) continue;
                String num;
                if (ensureBackupConfigForToolsRow(row, num) && backupAllPartitionsForApp(num)) {
                    backedUp++;
                }
            }
            if (backedUp == 0) {
                launcherUiInfo("No installed Apps with data partitions found to back up.", "Back Up All");
            } else {
                char msg[64];
                snprintf(msg, sizeof(msg), "Backed up data for %d %s.",
                         backedUp, (backedUp == 1) ? "App" : "Apps");
                launcherUiFinish(true, "Backups Complete", msg);
            }
            if (s_storeBody) renderActiveTab();
            break;
        }
        case PendingToolsAction::BackupApp:
            if (idx < s_backupRows.size()) {
                ToolsBackupRow row = s_backupRows[idx];
                String num;
                if (!ensureBackupConfigForToolsRow(row, num)) {
                    launcherUiInfo("No data partitions registered for " + row.appName + ".", "Backup");
                } else if (!backupAllPartitionsForApp(num)) {
                    launcherUiError("Could not complete backup for " + row.appName + ".", "Backup Failed");
                }
                if (s_storeBody) renderActiveTab();
            }
            break;
        case PendingToolsAction::RestoreApp:
            if (idx < s_backupRows.size()) {
                ToolsBackupRow row = s_backupRows[idx];
                String msg = String("Overwrite ") + row.appName + "'s flash data with its latest SD backup?";
                if (launcherUiConfirm("Restore Data", msg.c_str(), "Restore", M3Color::PRIMARY)) {
                    if (!restoreLastBackupForApp(row.appNum)) {
                        launcherUiError("Could not restore backup for " + row.appName + ".", "Restore Failed");
                    }
                }
                if (s_storeBody) renderActiveTab();
            }
            break;
        case PendingToolsAction::ManageBackupEntry:
            if (idx < s_backupRows.size()) {
                ToolsBackupRow row = s_backupRows[idx];
                std::vector<LauncherUiOption> opts;
                if (row.isInstalled && row.dataPartCount > 0) {
                    opts.push_back({String(LV_SYMBOL_SAVE "  Back Up Now"), [row]() {
                        String num;
                        if (ensureBackupConfigForToolsRow(row, num)) {
                            backupAllPartitionsForApp(num);
                        }
                    }});
                }
                if (row.hasBackup) {
                    opts.push_back({String(LV_SYMBOL_REFRESH "  Restore Last Backup"), [row]() {
                        String msg = String("Overwrite ") + row.appName + "'s flash data with its latest SD backup?";
                        if (launcherUiConfirm("Restore Data", msg.c_str(), "Restore", M3Color::PRIMARY)) {
                            if (!restoreLastBackupForApp(row.appNum)) {
                                launcherUiError("Could not restore backup for " + row.appName + ".", "Restore Failed");
                            }
                        }
                    }});
                }
                opts.push_back({String(LV_SYMBOL_TRASH "  Remove Backup Record"), [row]() {
                    String msg = String("Remove ") + row.appName + " from /bkp/backupData.json?";
                    if (launcherUiConfirm("Remove Record?", msg.c_str(), "Remove", M3Color::CORAL)) {
                        removeInstalledFromConfig(row.appNum);
                        m3ShowToast("Backup record removed", 1300);
                    }
                }});
                launcherUiSelectOption(row.appName.c_str(), opts, row.appNum.c_str());
                if (s_storeBody) renderActiveTab();
            }
            break;
        case PendingToolsAction::CheckUpdates:
            runCheckForAppUpdates();
            break;
        case PendingToolsAction::StartWebUiSta: {
            String err;
            if (!launcherWebUiStart(false, &err)) {
                launcherUiError(err.isEmpty() ? String("Could not start Web Upload server.") : err, "Web Upload");
            } else {
                m3ShowToast("Web Upload started", 1400);
            }
            if (s_storeBody) renderActiveTab();
            break;
        }
        case PendingToolsAction::StartWebUiAp: {
            String err;
            if (!launcherWebUiStart(true, &err)) {
                launcherUiError(err.isEmpty() ? String("Could not start Wi-Fi hotspot.") : err, "Web Upload");
            } else {
                m3ShowToast("Hotspot started", 1400);
            }
            if (s_storeBody) renderActiveTab();
            break;
        }
        case PendingToolsAction::StopWebUi:
            launcherWebUiStop();
            m3ShowToast("Web Upload stopped", 1200);
            if (s_storeBody) renderActiveTab();
            break;
        case PendingToolsAction::LaunchLastWuiApp: {
            LauncherWebUiStatus st = launcherWebUiGetStatus();
            if (st.lastInstalledLabel[0]) {
                launcherWebUiStop();
                launcherBootAppByLabel(st.lastInstalledLabel);
            }
            break;
        }
        case PendingToolsAction::ResetBootToFactory: {
            LauncherPartitionTable table;
            String err;
            if (!launcherPartitionReadCurrent(table, &err) || !launcherPartitionClearOtaBoot(table, &err)) {
                launcherUiError(err.isEmpty() ? String("Could not reset boot partition.") : err, "Boot Target");
            } else {
                g_launcherBootToApp = false;
                launcherUiSaveState();
                m3ShowToast("Next boot: Pixel CYD", 1500);
            }
            if (s_storeBody) renderActiveTab();
            break;
        }
        case PendingToolsAction::EditDeviceTag:
            openTextPromptModal("Device Tag", "e.g. CYD, CYD-2USB, ESP32", g_launcherOtaTag.c_str(), 24, "Save",
                                [](const char* text) {
                                    String tag(text ? text : "");
                                    tag.trim();
                                    if (!appStoreIsValidDeviceTag(tag.c_str())) {
                                        launcherUiError("Use 1-24 letters, digits, '-' or '_'.", "Invalid Tag");
                                        return;
                                    }
                                    launcherSetOtaTag(tag);
                                    s_exploreLoaded = false;
                                    s_exploreItems.clear();
                                    m3ShowToast(("Tag set to " + tag).c_str(), 1400);
                                    if (s_storeBody) renderActiveTab();
                                });
            break;
        case PendingToolsAction::ClearAppRegistry: {
            const char* body =
                "Clears saved App names, data links, and Home Screen recency order in NVS.\n"
                "Installed flash partitions are not erased.";
            if (launcherUiConfirm("Clear App Registry?", body, "Clear", M3Color::CORAL)) {
                if (launcherClearAppRegistry()) {
                    launcherHomeSyncRefresh(true, false);
                    m3ShowToast("App registry cleared", 1500);
                } else {
                    launcherUiError("Could not clear App registry.", "Registry");
                }
                if (s_storeBody) renderActiveTab();
            }
            break;
        }
        default:
            break;
    }
}

void queueToolsAction(PendingToolsAction action, size_t idx) {
    s_pendingToolsAction = action;
    s_pendingToolsIdx = idx;
    lv_async_call(asyncDispatchToolsAction, nullptr);
}

void toolsNavSubViewCb(lv_event_t* e) {
    m3PulseFeedback();
    int v = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    s_toolsSubView = static_cast<ToolsSubView>(v);
    renderActiveTab();
}

void toolsCompactBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::CompactFlash);
}

void toolsDumpTableBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::DumpTable);
}

void toolsPartitionRowCb(lv_event_t* e) {
    m3PulseFeedback();
    size_t idx = static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    queueToolsAction(PendingToolsAction::PartitionMenu, idx);
}

void toolsBackupAllBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::BackupAllApps);
}

void toolsBackupRowBtnCb(lv_event_t* e) {
    m3PulseFeedback();
    uintptr_t packed = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    PendingToolsAction action = static_cast<PendingToolsAction>((packed >> 8) & 0xFFu);
    size_t idx = static_cast<size_t>(packed & 0xFFu);
    queueToolsAction(action, idx);
}

void toolsCheckUpdatesBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::CheckUpdates);
}

void toolsRefreshSubViewCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    renderActiveTab();
}

lv_obj_t* createToolsMenuRow(lv_obj_t* container, const char* sym, uint32_t badgeBg, uint32_t badgeFg,
                             const char* title, const char* subtitle, lv_event_cb_t cb, void* userData) {
    lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_column(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, userData);

    lv_obj_t* badge = lv_obj_create(card);
    lv_obj_set_size(badge, 30, 30);
    lv_obj_set_style_radius(badge, 9, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(badgeBg), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* ico = lv_label_create(badge);
    lv_label_set_text(ico, sym);
    lv_obj_set_style_text_color(ico, lv_color_hex(badgeFg), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_12, 0);
    lv_obj_center(ico);

    lv_obj_t* col = lv_obj_create(card);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* tLbl = lv_label_create(col);
    lv_label_set_text(tLbl, title);
    lv_obj_set_width(tLbl, LV_PCT(100));
    lv_label_set_long_mode(tLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_12, 0);

    lv_obj_t* sLbl = lv_label_create(col);
    lv_label_set_text(sLbl, subtitle);
    lv_obj_set_width(sLbl, LV_PCT(100));
    lv_label_set_long_mode(sLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(sLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* chev = lv_label_create(card);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_10, 0);
    return card;
}

void buildToolsSubHeader(lv_obj_t* container, const char* title) {
    lv_obj_t* row = lv_obj_create(container);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* backBtn = m3PillBtn(row, LV_SYMBOL_LEFT " Tools", m3SurfaceHighHex(), m3TextPrimaryHex(), 62, 26);
    lv_obj_add_event_cb(backBtn, toolsNavSubViewCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(ToolsSubView::Menu)));

    lv_obj_t* tLbl = lv_label_create(row);
    lv_label_set_text(tLbl, title);
    lv_obj_set_flex_grow(tLbl, 1);
    lv_label_set_long_mode(tLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(tLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_12, 0);

    lv_obj_t* refBtn = m3PillBtn(row, LV_SYMBOL_REFRESH, m3SurfaceHighHex(), m3TextSecondaryHex(), 26, 26);
    lv_obj_add_event_cb(refBtn, toolsRefreshSubViewCb, LV_EVENT_CLICKED, nullptr);
}

void buildToolsMenuView(lv_obj_t* container) {
    LauncherPartitionTable table;
    bool hasTable = launcherPartitionReadCurrent(table);
    AppStoreStorageSummary storage = computeLiveStorageSummary(hasTable ? &table : nullptr, appGetDynamicCount());

    // 1. Visual flash usage summary card
    lv_obj_t* sumCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(sumCard, 8, 0);
    lv_obj_set_style_pad_row(sumCard, 5, 0);
    lv_obj_set_flex_flow(sumCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* topRow = lv_obj_create(sumCard);
    lv_obj_set_size(topRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* fTitle = lv_label_create(topRow);
    lv_label_set_text(fTitle, "Flash Storage");
    lv_obj_set_style_text_color(fTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(fTitle, &lv_font_montserrat_12, 0);

    char freeSz[20], usedSz[20];
    appStoreFormatSizeCompact(storage.freeBytes, freeSz, sizeof(freeSz));
    appStoreFormatSizeCompact(storage.usedBytes, usedSz, sizeof(usedSz));
    char statBuf[48];
    snprintf(statBuf, sizeof(statBuf), "%s used • %s free", usedSz, freeSz);

    lv_obj_t* fStat = lv_label_create(topRow);
    lv_label_set_text(fStat, statBuf);
    lv_obj_set_style_text_color(fStat, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(fStat, &lv_font_montserrat_10, 0);

    if (hasTable) {
        buildFlashMapBar(sumCard, table);
    }

    // 2. Main Tool cards
    char pmanSub[52];
    snprintf(pmanSub, sizeof(pmanSub), "%u partitions • Compact, dump & restore",
             static_cast<unsigned>(hasTable ? table.entries.size() : 0));
    createToolsMenuRow(container, LV_SYMBOL_DRIVE, M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                       "Storage & Partitions", pmanSub,
                       toolsNavSubViewCb,
                       reinterpret_cast<void*>(static_cast<intptr_t>(ToolsSubView::StoragePartitions)));

    createToolsMenuRow(container, LV_SYMBOL_SAVE, 0x1E3A2F, M3Color::TERTIARY,
                       "Backup & Restore", "Back up & restore App data on SD",
                       toolsNavSubViewCb,
                       reinterpret_cast<void*>(static_cast<intptr_t>(ToolsSubView::BackupRestore)));

    createToolsMenuRow(container, LV_SYMBOL_DOWNLOAD, 0x3D2E14, M3Color::AMBER,
                       "Check for App Updates", "Scan SD downloads on LauncherHub",
                       toolsCheckUpdatesBtnCb, nullptr);

    createToolsMenuRow(container, LV_SYMBOL_REFRESH, m3SurfaceHighestHex(), m3TextPrimaryHex(),
                       "Sync Home Screen", "Refresh dynamic App tiles on Home",
                       syncRefreshBtnCb, nullptr);

    // 3. Web Upload (WUI) & Store Settings
    LauncherWebUiStatus wuiSt = launcherWebUiGetStatus();
    String wuiSub = wuiSt.running ? (String("Online • ") + wuiSt.url)
                                  : String("Upload & flash .bin from browser");
    createToolsMenuRow(container, LV_SYMBOL_WIFI,
                       wuiSt.running ? 0x1E3A2F : M3Color::PRIMARY_CONT,
                       wuiSt.running ? M3Color::TERTIARY : M3Color::PRIMARY,
                       "Web Upload (WUI)", wuiSub.c_str(),
                       toolsNavSubViewCb,
                       reinterpret_cast<void*>(static_cast<intptr_t>(ToolsSubView::WebUpload)));

    char setSub[56];
    snprintf(setSub, sizeof(setSub), "Tag: %s • %s",
             g_launcherOtaTag.c_str(),
             g_launcherBootToApp ? "Boot App" : "Boot Store");
    createToolsMenuRow(container, LV_SYMBOL_SETTINGS, m3SurfaceHighestHex(), m3TextPrimaryHex(),
                       "Store Settings", setSub,
                       toolsNavSubViewCb,
                       reinterpret_cast<void*>(static_cast<intptr_t>(ToolsSubView::Settings)));
}

void buildStoragePartitionsView(lv_obj_t* container) {
    buildToolsSubHeader(container, "Storage & Partitions");

    LauncherPartitionTable table;
    String readErr;
    if (!launcherPartitionReadCurrent(table, &readErr)) {
        lv_obj_t* errCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_t* lbl = lv_label_create(errCard);
        lv_label_set_text(lbl, ("Could not read partition table:\n" + readErr).c_str());
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::CORAL), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        return;
    }

    s_pmanEntries = table.entries;
    AppStoreStorageSummary storage = computeLiveStorageSummary(&table, launcherPartitionCountOtaApps(table));
    uint32_t largestFree = 0;
    bool canCompact = launcherCanCompactPartitions(table, &largestFree);

    // 1. Visual map & compaction card
    lv_obj_t* mapCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(mapCard, 8, 0);
    lv_obj_set_style_pad_row(mapCard, 5, 0);
    lv_obj_set_flex_flow(mapCard, LV_FLEX_FLOW_COLUMN);

    buildFlashMapBar(mapCard, table);

    char totalSz[20], freeSz[20], bigSz[20];
    appStoreFormatSizeCompact(table.flashSize, totalSz, sizeof(totalSz));
    appStoreFormatSizeCompact(storage.freeBytes, freeSz, sizeof(freeSz));
    appStoreFormatSizeCompact(largestFree, bigSz, sizeof(bigSz));

    m3InfoRow(mapCard, "Flash / Free", (String(totalSz) + " / " + freeSz).c_str());
    m3InfoRow(mapCard, "Largest Free Block", bigSz);
    m3InfoRow(mapCard, "Layout Status", canCompact ? "Fragmented (compactable)" : "Compact");

    lv_obj_t* actRow = lv_obj_create(mapCard);
    lv_obj_set_size(actRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actRow, 0, 0);
    lv_obj_set_style_pad_all(actRow, 0, 0);
    lv_obj_set_style_pad_column(actRow, 6, 0);
    lv_obj_set_flex_flow(actRow, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(actRow, LV_OBJ_FLAG_SCROLLABLE);

    createCompactChip(actRow, LV_SYMBOL_SHUFFLE " Compact",
                      canCompact ? M3Color::PRIMARY : m3SurfaceHighestHex(),
                      canCompact ? M3Color::ON_PRIMARY : m3TextSecondaryHex(),
                      toolsCompactBtnCb, nullptr);
    createCompactChip(actRow, LV_SYMBOL_SAVE " Dump Table",
                      M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                      toolsDumpTableBtnCb, nullptr);

    // 2. Partition list (map friendly App names for OTA slots)
    std::vector<LauncherAppMetadata> apps = launcherListInstalledApps();
    for (size_t i = 0; i < s_pmanEntries.size(); ++i) {
        const LauncherPartitionEntry& entry = s_pmanEntries[i];
        uint32_t colHex = appStorePartitionColorHex(entry.type, entry.subtype);
        const char* tag = appStorePartitionShortTag(entry.type, entry.subtype);

        String displayTitle(entry.label[0] ? entry.label : "unnamed");
        if (entry.isOtaApp()) {
            for (const LauncherAppMetadata& app : apps) {
                if (app.label == entry.label && !app.name.isEmpty() && app.name != app.label) {
                    displayTitle = app.name + " (" + app.label + ")";
                    break;
                }
            }
        }

        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 12);
        lv_obj_set_style_pad_all(card, 7, 0);
        lv_obj_set_style_pad_column(card, 7, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, toolsPartitionRowCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        lv_obj_t* badge = lv_obj_create(card);
        lv_obj_set_size(badge, 32, 24);
        lv_obj_set_style_radius(badge, 7, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(colHex), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* tagLbl = lv_label_create(badge);
        lv_label_set_text(tagLbl, tag);
        lv_obj_set_style_text_color(tagLbl, lv_color_hex(0x0E1117), 0);
        lv_obj_set_style_text_font(tagLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(tagLbl);

        lv_obj_t* col = lv_obj_create(card);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 1, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* nameLbl = lv_label_create(col);
        lv_label_set_text(nameLbl, displayTitle.c_str());
        lv_obj_set_width(nameLbl, LV_PCT(100));
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);

        char metaBuf[64];
        appStoreFormatPartitionMeta(entry.offset, entry.size, entry.type, entry.subtype, metaBuf, sizeof(metaBuf));
        lv_obj_t* metaLbl = lv_label_create(col);
        lv_label_set_text(metaLbl, metaBuf);
        lv_obj_set_width(metaLbl, LV_PCT(100));
        lv_label_set_long_mode(metaLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(metaLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(metaLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* chev = lv_label_create(card);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_10, 0);
    }
}

void buildBackupRestoreView(lv_obj_t* container) {
    buildToolsSubHeader(container, "Backup & Restore");

    if (!isSdCardMounted() && !setupSdCard()) {
        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, LV_SYMBOL_SD_CARD "  SD Card Required");
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

        lv_obj_t* desc = lv_label_create(card);
        lv_label_set_text(desc, "Insert a FAT32 microSD card to back up or restore App data partitions (/bkp/).");
        lv_obj_set_width(desc, LV_PCT(100));
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);

        lv_obj_t* btn = m3PillBtn(card, "Mount SD Card", M3Color::PRIMARY, M3Color::ON_PRIMARY, 136, 30);
        lv_obj_add_event_cb(btn, checkSdBtnCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    s_backupRows = collectToolsBackupRows();
    int installableCount = 0;
    int readyCount = 0;
    for (const ToolsBackupRow& r : s_backupRows) {
        if (r.isInstalled && r.dataPartCount > 0) installableCount++;
        if (r.hasBackup) readyCount++;
    }

    // Summary & Back Up All card
    lv_obj_t* sumCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(sumCard, 8, 0);
    lv_obj_set_style_pad_row(sumCard, 6, 0);
    lv_obj_set_flex_flow(sumCard, LV_FLEX_FLOW_COLUMN);

    char statBuf[56];
    snprintf(statBuf, sizeof(statBuf), "%d Apps with data • %d saved on SD", installableCount, readyCount);
    lv_obj_t* sLbl = lv_label_create(sumCard);
    lv_label_set_text(sLbl, statBuf);
    lv_obj_set_style_text_color(sLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);

    if (installableCount > 0) {
        lv_obj_t* allBtn = m3PillBtn(sumCard, LV_SYMBOL_SAVE "  Back Up All Installed Apps",
                                     M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 28);
        lv_obj_add_event_cb(allBtn, toolsBackupAllBtnCb, LV_EVENT_CLICKED, nullptr);
    }

    if (s_backupRows.empty()) {
        lv_obj_t* emptyCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
        lv_obj_set_style_pad_all(emptyCard, 12, 0);
        lv_obj_set_style_pad_row(emptyCard, 6, 0);
        lv_obj_set_flex_flow(emptyCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(emptyCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* eTitle = lv_label_create(emptyCard);
        lv_label_set_text(eTitle, "No App Data Backups");
        lv_obj_set_style_text_color(eTitle, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(eTitle, &lv_font_montserrat_12, 0);

        lv_obj_t* eDesc = lv_label_create(emptyCard);
        lv_label_set_text(eDesc, "Apps with SPIFFS, FAT, or LittleFS data partitions will appear here for one-tap SD backup and restore.");
        lv_obj_set_width(eDesc, LV_PCT(100));
        lv_label_set_long_mode(eDesc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(eDesc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(eDesc, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(eDesc, &lv_font_montserrat_10, 0);
        return;
    }

    for (size_t i = 0; i < s_backupRows.size(); ++i) {
        const ToolsBackupRow& row = s_backupRows[i];

        lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 13);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_pad_row(card, 5, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* head = lv_obj_create(card);
        lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(head, 0, 0);
        lv_obj_set_style_pad_all(head, 0, 0);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(head, 1, 0);
        lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* nLbl = lv_label_create(head);
        lv_label_set_text(nLbl, row.appName.c_str());
        lv_obj_set_width(nLbl, LV_PCT(100));
        lv_label_set_long_mode(nLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(nLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(nLbl, &lv_font_montserrat_12, 0);

        char subBuf[64];
        if (!row.isInstalled) {
            snprintf(subBuf, sizeof(subBuf), "Archived • %s",
                     row.hasBackup ? "Backup on SD" : "No SD files");
        } else {
            snprintf(subBuf, sizeof(subBuf), "%u data %s • %s",
                     static_cast<unsigned>(row.dataPartCount),
                     (row.dataPartCount == 1) ? "part" : "parts",
                     row.hasBackup ? "Backup ready" : "No backup yet");
        }
        lv_obj_t* subLbl = lv_label_create(head);
        lv_label_set_text(subLbl, subBuf);
        lv_obj_set_width(subLbl, LV_PCT(100));
        lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(subLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* btnRow = lv_obj_create(card);
        lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, 5, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

        if (row.isInstalled && row.dataPartCount > 0) {
            uintptr_t packed = (static_cast<uintptr_t>(PendingToolsAction::BackupApp) << 8) | (i & 0xFFu);
            createCompactChip(btnRow, LV_SYMBOL_SAVE " Back Up",
                              M3Color::PRIMARY_CONT, M3Color::PRIMARY,
                              toolsBackupRowBtnCb, reinterpret_cast<void*>(packed));
        }
        if (row.hasBackup) {
            uintptr_t packed = (static_cast<uintptr_t>(PendingToolsAction::RestoreApp) << 8) | (i & 0xFFu);
            createCompactChip(btnRow, LV_SYMBOL_REFRESH " Restore",
                              0x1E3A2F, M3Color::TERTIARY,
                              toolsBackupRowBtnCb, reinterpret_cast<void*>(packed));
        }
        uintptr_t packedMgr = (static_cast<uintptr_t>(PendingToolsAction::ManageBackupEntry) << 8) | (i & 0xFFu);
        createCompactChip(btnRow, "More",
                          m3SurfaceHighestHex(), m3TextSecondaryHex(),
                          toolsBackupRowBtnCb, reinterpret_cast<void*>(packedMgr));
    }
}

// --- Web Upload (WUI) & Store Settings Sub-Views ---

void stopWuiTimer() {
    if (s_wuiTimer) {
        lv_timer_del(s_wuiTimer);
        s_wuiTimer = nullptr;
    }
    s_wuiStatusLbl = nullptr;
    s_wuiProgBar = nullptr;
    s_wuiTransferLbl = nullptr;
}

void wuiPollTimerCb(lv_timer_t* timer) {
    (void)timer;
    launcherWebUiPoll();

    if (!s_storeBody || s_activeTab != AppStoreTab::Tools || s_toolsSubView != ToolsSubView::WebUpload) {
        return;
    }

    LauncherWebUiStatus st = launcherWebUiGetStatus();
    if (!st.running) {
        stopWuiTimer();
        renderActiveTab();
        return;
    }

    if (s_wuiStatusLbl) {
        lv_label_set_text(s_wuiStatusLbl, st.statusText[0] ? st.statusText : "Waiting for browser...");
        uint32_t col = m3TextPrimaryHex();
        if (st.state == LauncherWebUiState::Success) col = M3Color::TERTIARY;
        else if (st.state == LauncherWebUiState::Error) col = M3Color::CORAL;
        else if (st.state == LauncherWebUiState::Flashing || st.state == LauncherWebUiState::Uploading) col = M3Color::PRIMARY;
        lv_obj_set_style_text_color(s_wuiStatusLbl, lv_color_hex(col), 0);
    }
    if (s_wuiProgBar) {
        bool active = (st.state == LauncherWebUiState::Uploading ||
                       st.state == LauncherWebUiState::Flashing ||
                       st.state == LauncherWebUiState::Success);
        if (active) {
            lv_obj_clear_flag(s_wuiProgBar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_wuiProgBar, st.progressPct, LV_ANIM_ON);
        } else {
            lv_obj_add_flag(s_wuiProgBar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_wuiTransferLbl) {
        char xfer[48];
        appStoreFormatWebUiTransfer(st.bytesDone, st.bytesTotal, st.progressPct, xfer, sizeof(xfer));
        lv_label_set_text(s_wuiTransferLbl, xfer);
    }

    if (st.eventSeq != s_wuiLastEventSeq) {
        s_wuiLastEventSeq = st.eventSeq;
        renderActiveTab();
    }
}

void toolsStartWuiStaCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::StartWebUiSta);
}

void toolsStartWuiApCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::StartWebUiAp);
}

void toolsStopWuiCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::StopWebUi);
}

void toolsLaunchLastWuiCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::LaunchLastWuiApp);
}

void buildWebUploadView(lv_obj_t* container) {
    buildToolsSubHeader(container, "Web Upload (WUI)");
    LauncherWebUiStatus st = launcherWebUiGetStatus();
    s_wuiLastEventSeq = st.eventSeq;

    if (!st.running) {
        stopWuiTimer();

        lv_obj_t* hero = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
        lv_obj_set_style_pad_all(hero, 12, 0);
        lv_obj_set_style_pad_row(hero, 8, 0);
        lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* ico = lv_label_create(hero);
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(ico, lv_color_hex(m3PrimaryHex()), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);

        lv_obj_t* title = lv_label_create(hero);
        lv_label_set_text(title, "Local Browser Upload");
        lv_obj_set_style_text_color(title, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

        lv_obj_t* desc = lv_label_create(hero);
        lv_label_set_text(desc, "Flash .bin Apps directly or upload files to SD from any phone or computer browser. Installed Apps sync to Home Screen #1.");
        lv_obj_set_width(desc, LV_PCT(100));
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);

        bool wifiUp = (WiFi.status() == WL_CONNECTED);
        if (wifiUp) {
            String staLabel = String(LV_SYMBOL_WIFI "  Start on ") + WiFi.SSID();
            lv_obj_t* staBtn = m3PillBtn(hero, staLabel.c_str(), M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 32);
            lv_obj_add_event_cb(staBtn, toolsStartWuiStaCb, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* apBtn = m3PillBtn(hero, LV_SYMBOL_USB "  Start Hotspot (AP)",
                                        m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 28);
            lv_obj_add_event_cb(apBtn, toolsStartWuiApCb, LV_EVENT_CLICKED, nullptr);
        } else {
            lv_obj_t* apBtn = m3PillBtn(hero, LV_SYMBOL_WIFI "  Start Hotspot (AP)",
                                        M3Color::PRIMARY, M3Color::ON_PRIMARY, LV_PCT(100), 32);
            lv_obj_add_event_cb(apBtn, toolsStartWuiApCb, LV_EVENT_CLICKED, nullptr);

            lv_obj_t* wifiBtn = m3PillBtn(hero, "Open Wi-Fi Settings",
                                          m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 28);
            lv_obj_add_event_cb(wifiBtn, openWifiSettingsCb, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }

    // Active Server: QR Code + IP Card
    lv_obj_t* qrCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_style_pad_all(qrCard, 10, 0);
    lv_obj_set_style_pad_row(qrCard, 6, 0);
    lv_obj_set_flex_flow(qrCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qrCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* qrWrap = lv_obj_create(qrCard);
    lv_obj_set_size(qrWrap, 108, 108);
    lv_obj_set_style_bg_color(qrWrap, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(qrWrap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qrWrap, 10, 0);
    lv_obj_set_style_border_width(qrWrap, 0, 0);
    lv_obj_set_style_pad_all(qrWrap, 6, 0);
    lv_obj_clear_flag(qrWrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* qr = lv_qrcode_create(qrWrap, 96, lv_color_hex(0x0E1117), lv_color_hex(0xFFFFFF));
    if (qr && st.url[0]) {
        lv_qrcode_update(qr, st.url, strlen(st.url));
        lv_obj_center(qr);
    }

    lv_obj_t* urlLbl = lv_label_create(qrCard);
    lv_label_set_text(urlLbl, st.url);
    lv_obj_set_style_text_color(urlLbl, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(urlLbl, &lv_font_montserrat_14, 0);

    char netBuf[64];
    snprintf(netBuf, sizeof(netBuf), "%s: %s%s",
             st.apMode ? "Hotspot" : "Wi-Fi",
             st.ssid[0] ? st.ssid : "Connected",
             st.apMode ? " (open)" : "");
    lv_obj_t* netLbl = lv_label_create(qrCard);
    lv_label_set_text(netLbl, netBuf);
    lv_obj_set_style_text_color(netLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(netLbl, &lv_font_montserrat_10, 0);

    // Live Upload / Flash Status Card
    lv_obj_t* stCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(stCard, 8, 0);
    lv_obj_set_style_pad_row(stCard, 4, 0);
    lv_obj_set_flex_flow(stCard, LV_FLEX_FLOW_COLUMN);

    s_wuiStatusLbl = lv_label_create(stCard);
    lv_label_set_text(s_wuiStatusLbl, st.statusText[0] ? st.statusText : "Waiting for browser...");
    lv_obj_set_width(s_wuiStatusLbl, LV_PCT(100));
    lv_label_set_long_mode(s_wuiStatusLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_wuiStatusLbl,
                                lv_color_hex(st.state == LauncherWebUiState::Success ? M3Color::TERTIARY
                                             : st.state == LauncherWebUiState::Error ? M3Color::CORAL
                                                                                     : m3TextPrimaryHex()),
                                0);
    lv_obj_set_style_text_font(s_wuiStatusLbl, &lv_font_montserrat_12, 0);

    s_wuiProgBar = lv_bar_create(stCard);
    lv_obj_set_size(s_wuiProgBar, LV_PCT(100), 8);
    lv_bar_set_range(s_wuiProgBar, 0, 100);
    lv_bar_set_value(s_wuiProgBar, st.progressPct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_wuiProgBar, lv_color_hex(m3SurfaceHighestHex()), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_wuiProgBar, lv_color_hex(M3Color::PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_wuiProgBar, M3Dim::RADIUS_PILL, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wuiProgBar, M3Dim::RADIUS_PILL, LV_PART_INDICATOR);
    if (st.state == LauncherWebUiState::Idle || st.state == LauncherWebUiState::Error) {
        lv_obj_add_flag(s_wuiProgBar, LV_OBJ_FLAG_HIDDEN);
    }

    char xfer[48];
    appStoreFormatWebUiTransfer(st.bytesDone, st.bytesTotal, st.progressPct, xfer, sizeof(xfer));
    s_wuiTransferLbl = lv_label_create(stCard);
    lv_label_set_text(s_wuiTransferLbl, xfer);
    lv_obj_set_style_text_color(s_wuiTransferLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(s_wuiTransferLbl, &lv_font_montserrat_10, 0);

    if (st.lastInstalledLabel[0]) {
        String launchTxt = String(LV_SYMBOL_PLAY "  Launch ") +
                           (st.lastInstalledName[0] ? st.lastInstalledName : st.lastInstalledLabel);
        lv_obj_t* launchBtn = m3PillBtn(stCard, launchTxt.c_str(), M3Color::TERTIARY, 0x0E1117, LV_PCT(100), 30);
        lv_obj_add_event_cb(launchBtn, toolsLaunchLastWuiCb, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* stopBtn = m3PillBtn(container, LV_SYMBOL_CLOSE "  Stop Web Server",
                                  M3Color::CORAL, 0x0E1117, LV_PCT(100), 30);
    lv_obj_add_event_cb(stopBtn, toolsStopWuiCb, LV_EVENT_CLICKED, nullptr);

    if (!s_wuiTimer) {
        s_wuiTimer = lv_timer_create(wuiPollTimerCb, 250, nullptr);
    }
}

void settingsBootSwitchCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_launcherBootToApp = lv_obj_has_state(sw, LV_STATE_CHECKED);
    launcherUiSaveState();
    m3PulseFeedback();
    m3ShowToast(appStoreBootPrefLabel(g_launcherBootToApp), 1300);
    if (s_storeBody) renderActiveTab();
}

void settingsAutoBackupSwitchCb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    g_launcherAutoBackup = lv_obj_has_state(sw, LV_STATE_CHECKED);
    launcherUiSaveState();
    m3PulseFeedback();
    m3ShowToast(g_launcherAutoBackup ? "Auto-backup enabled" : "Auto-backup disabled", 1200);
}

void settingsResetBootBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::ResetBootToFactory);
}

void settingsEditTagBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::EditDeviceTag);
}

void settingsPresetTagCb(lv_event_t* e) {
    m3PulseFeedback();
    const char* preset = static_cast<const char*>(lv_event_get_user_data(e));
    if (!preset || !preset[0]) return;
    launcherSetOtaTag(String(preset));
    s_exploreLoaded = false;
    s_exploreItems.clear();
    m3ShowToast((String("Tag: ") + preset).c_str(), 1200);
    renderActiveTab();
}

void settingsClearRegistryCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    queueToolsAction(PendingToolsAction::ClearAppRegistry);
}

void buildSettingsView(lv_obj_t* container) {
    buildToolsSubHeader(container, "Store Settings");

    // 1. Boot Target Preference
    lv_obj_t* bootCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(bootCard, 9, 0);
    lv_obj_set_style_pad_row(bootCard, 6, 0);
    lv_obj_set_flex_flow(bootCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* bootRow = lv_obj_create(bootCard);
    lv_obj_set_size(bootRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bootRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bootRow, 0, 0);
    lv_obj_set_style_pad_all(bootRow, 0, 0);
    lv_obj_set_flex_flow(bootRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bootRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bootRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bCol = lv_obj_create(bootRow);
    lv_obj_set_height(bCol, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bCol, 1);
    lv_obj_set_style_bg_opa(bCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bCol, 0, 0);
    lv_obj_set_style_pad_all(bCol, 0, 0);
    lv_obj_set_flex_flow(bCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bCol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bTitle = lv_label_create(bCol);
    lv_label_set_text(bTitle, "Boot Into Last App");
    lv_obj_set_style_text_color(bTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(bTitle, &lv_font_montserrat_12, 0);

    lv_obj_t* bSub = lv_label_create(bCol);
    lv_label_set_text(bSub, appStoreBootPrefLabel(g_launcherBootToApp));
    lv_obj_set_style_text_color(bSub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(bSub, &lv_font_montserrat_10, 0);

    lv_obj_t* bootSw = lv_switch_create(bootRow);
    lv_obj_set_size(bootSw, 40, 22);
    if (g_launcherBootToApp) lv_obj_add_state(bootSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(bootSw, settingsBootSwitchCb, LV_EVENT_VALUE_CHANGED, nullptr);

    const esp_partition_t* bootPart = esp_ota_get_boot_partition();
    bool bootingOta = bootPart && bootPart->type == ESP_PARTITION_TYPE_APP &&
                      bootPart->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0;
    String activeSlot = bootingOta ? launcherAppDisplayNameForLabel(bootPart->label)
                                   : String("Pixel CYD (factory)");
    m3InfoRow(bootCard, "Next Boot Slot", activeSlot.c_str());

    if (bootingOta) {
        lv_obj_t* rstBtn = m3PillBtn(bootCard, LV_SYMBOL_HOME "  Reset Boot to Pixel CYD",
                                     M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 26);
        lv_obj_add_event_cb(rstBtn, settingsResetBootBtnCb, LV_EVENT_CLICKED, nullptr);
    }

    // 2. Auto-Backup Toggle
    lv_obj_t* bkpCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(bkpCard, 9, 0);
    lv_obj_set_flex_flow(bkpCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bkpCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* bkCol = lv_obj_create(bkpCard);
    lv_obj_set_height(bkCol, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bkCol, 1);
    lv_obj_set_style_bg_opa(bkCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bkCol, 0, 0);
    lv_obj_set_style_pad_all(bkCol, 0, 0);
    lv_obj_set_flex_flow(bkCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bkCol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bkTitle = lv_label_create(bkCol);
    lv_label_set_text(bkTitle, "Auto-Backup App Data");
    lv_obj_set_style_text_color(bkTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(bkTitle, &lv_font_montserrat_12, 0);

    lv_obj_t* bkSub = lv_label_create(bkCol);
    lv_label_set_text(bkSub, "Save data partitions to SD");
    lv_obj_set_style_text_color(bkSub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(bkSub, &lv_font_montserrat_10, 0);

    lv_obj_t* bkSw = lv_switch_create(bkpCard);
    lv_obj_set_size(bkSw, 40, 22);
    if (g_launcherAutoBackup) lv_obj_add_state(bkSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(bkSw, settingsAutoBackupSwitchCb, LV_EVENT_VALUE_CHANGED, nullptr);

    // 3. Device Tag (LauncherHub hardware filter)
    lv_obj_t* tagCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(tagCard, 9, 0);
    lv_obj_set_style_pad_row(tagCard, 6, 0);
    lv_obj_set_flex_flow(tagCard, LV_FLEX_FLOW_COLUMN);

    m3InfoRow(tagCard, "Device Tag", g_launcherOtaTag.c_str());

    lv_obj_t* presetRow = lv_obj_create(tagCard);
    lv_obj_set_size(presetRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(presetRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetRow, 0, 0);
    lv_obj_set_style_pad_all(presetRow, 0, 0);
    lv_obj_set_style_pad_column(presetRow, 4, 0);
    lv_obj_set_flex_flow(presetRow, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(presetRow, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kTagPresets[3] = {"CYD", "CYD-2USB", "ESP32"};
    for (int i = 0; i < 3; ++i) {
        bool active = (g_launcherOtaTag == kTagPresets[i]);
        createCompactChip(presetRow, kTagPresets[i],
                          active ? M3Color::PRIMARY_CONT : m3SurfaceHighestHex(),
                          active ? M3Color::PRIMARY : m3TextSecondaryHex(),
                          settingsPresetTagCb, const_cast<char*>(kTagPresets[i]));
    }

    lv_obj_t* editTagBtn = m3PillBtn(tagCard, LV_SYMBOL_EDIT "  Custom Device Tag",
                                     m3SurfaceHighestHex(), m3TextPrimaryHex(), LV_PCT(100), 26);
    lv_obj_add_event_cb(editTagBtn, settingsEditTagBtnCb, LV_EVENT_CLICKED, nullptr);

    // 4. Clear App Registry
    lv_obj_t* regCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_all(regCard, 9, 0);
    lv_obj_set_style_pad_row(regCard, 6, 0);
    lv_obj_set_flex_flow(regCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* rTitle = lv_label_create(regCard);
    lv_label_set_text(rTitle, "App Registry (NVS)");
    lv_obj_set_style_text_color(rTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(rTitle, &lv_font_montserrat_12, 0);

    lv_obj_t* rDesc = lv_label_create(regCard);
    lv_label_set_text(rDesc, "Resets stored App names, Home Screen order, and data partition links.");
    lv_obj_set_width(rDesc, LV_PCT(100));
    lv_label_set_long_mode(rDesc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(rDesc, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(rDesc, &lv_font_montserrat_10, 0);

    lv_obj_t* clrBtn = m3PillBtn(regCard, LV_SYMBOL_TRASH "  Clear App Registry",
                                 M3Color::CORAL, 0x0E1117, LV_PCT(100), 28);
    lv_obj_add_event_cb(clrBtn, settingsClearRegistryCb, LV_EVENT_CLICKED, nullptr);
}

void buildToolsTab(lv_obj_t* container) {
    switch (s_toolsSubView) {
        case ToolsSubView::StoragePartitions:
            buildStoragePartitionsView(container);
            break;
        case ToolsSubView::BackupRestore:
            buildBackupRestoreView(container);
            break;
        case ToolsSubView::WebUpload:
            buildWebUploadView(container);
            break;
        case ToolsSubView::Settings:
            buildSettingsView(container);
            break;
        case ToolsSubView::Menu:
        default:
            buildToolsMenuView(container);
            break;
    }
}

void updateTabBarStyle() {
    if (!s_tabBar) return;
    lv_obj_set_style_bg_color(s_tabBar, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_border_color(s_tabBar, lv_color_hex(m3OutlineHex()), 0);

    for (uint8_t i = 0; i < kTabCount; ++i) {
        if (!s_tabBtns[i] || !s_tabLbls[i]) continue;
        bool active = (static_cast<uint8_t>(s_activeTab) == i);
        if (active) {
            lv_obj_set_style_bg_color(s_tabBtns[i], lv_color_hex(M3Color::PRIMARY_CONT), 0);
            lv_obj_set_style_bg_opa(s_tabBtns[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(s_tabLbls[i], lv_color_hex(M3Color::PRIMARY), 0);
        } else {
            lv_obj_set_style_bg_opa(s_tabBtns[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(s_tabLbls[i], lv_color_hex(m3TextSecondaryHex()), 0);
        }
    }
}

void renderActiveTab() {
    if (!s_tabContent) return;
    if (s_activeTab != AppStoreTab::Tools || s_toolsSubView != ToolsSubView::WebUpload) {
        s_wuiStatusLbl = nullptr;
        s_wuiProgBar = nullptr;
        s_wuiTransferLbl = nullptr;
    }
    lv_obj_clean(s_tabContent);
    lv_obj_scroll_to_y(s_tabContent, 0, LV_ANIM_OFF);

    switch (s_activeTab) {
        case AppStoreTab::Explore:
            buildExploreTab(s_tabContent);
            break;
        case AppStoreTab::SdCard:
            buildSdCardTab(s_tabContent);
            break;
        case AppStoreTab::Installed:
            buildInstalledTab(s_tabContent);
            break;
        case AppStoreTab::Tools:
            buildToolsTab(s_tabContent);
            break;
        default:
            buildInstalledTab(s_tabContent);
            break;
    }
}

void storeBodyDeleteCb(lv_event_t* e) {
    (void)e;
    stopWuiTimer();
    if (launcherWebUiIsRunning()) {
        launcherWebUiStop();
    }
    closeTextPromptModal();
    s_wuiStatusLbl = nullptr;
    s_wuiProgBar = nullptr;
    s_wuiTransferLbl = nullptr;
    // Release cached tab lists to return internal DRAM when leaving App Store.
    std::vector<InstalledRowData>().swap(s_installedRows);
    std::vector<ExploreCatalogItem>().swap(s_exploreItems);
    std::vector<ExploreVersionItem>().swap(s_exploreDetail.versions);
    s_exploreDetail.active = false;
    s_exploreLoaded = false;
    std::vector<LauncherSdEntry>().swap(s_sdEntries);
    std::vector<LauncherPartitionEntry>().swap(s_pmanEntries);
    std::vector<ToolsBackupRow>().swap(s_backupRows);
    g_launcherHubDoc.clear();
    s_storeBody = nullptr;
    s_tabBar = nullptr;
    s_tabContent = nullptr;
    for (uint8_t i = 0; i < kTabCount; ++i) {
        s_tabBtns[i] = nullptr;
        s_tabLbls[i] = nullptr;
    }
}

void asyncRefreshTabCb(void* arg) {
    (void)arg;
    s_asyncRefreshPending = false;
    if (!s_storeBody) return;
    if (s_activeTab == AppStoreTab::Installed || s_activeTab == AppStoreTab::Tools) {
        renderActiveTab();
    }
}

void buildAppStore(lv_obj_t* body) {
    s_storeBody = body;
    lv_obj_add_event_cb(body, storeBodyDeleteCb, LV_EVENT_DELETE, nullptr);

    // Edge-to-edge column layout: fixed segmented tab bar + scrollable tab content
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* barWrap = lv_obj_create(body);
    lv_obj_set_size(barWrap, LV_PCT(100), 38);
    lv_obj_set_style_bg_opa(barWrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(barWrap, 0, 0);
    lv_obj_set_style_radius(barWrap, 0, 0);
    lv_obj_set_style_pad_hor(barWrap, 6, 0);
    lv_obj_set_style_pad_ver(barWrap, 4, 0);
    lv_obj_clear_flag(barWrap, LV_OBJ_FLAG_SCROLLABLE);

    s_tabBar = lv_obj_create(barWrap);
    lv_obj_set_size(s_tabBar, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(s_tabBar, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(s_tabBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tabBar, 1, 0);
    lv_obj_set_style_border_color(s_tabBar, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_border_opa(s_tabBar, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_tabBar, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_pad_all(s_tabBar, 2, 0);
    lv_obj_set_style_pad_column(s_tabBar, 2, 0);
    lv_obj_set_flex_flow(s_tabBar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(s_tabBar, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < kTabCount; ++i) {
        lv_obj_t* btn = lv_btn_create(s_tabBar);
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, M3Dim::RADIUS_PILL, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, switchTabBtnCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(i)));

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, appStoreTabLabel(static_cast<AppStoreTab>(i)));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_center(lbl);

        s_tabBtns[i] = btn;
        s_tabLbls[i] = lbl;
    }

    s_tabContent = lv_obj_create(body);
    lv_obj_set_width(s_tabContent, LV_PCT(100));
    lv_obj_set_flex_grow(s_tabContent, 1);
    lv_obj_set_style_bg_opa(s_tabContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tabContent, 0, 0);
    lv_obj_set_style_radius(s_tabContent, 0, 0);
    lv_obj_set_style_pad_hor(s_tabContent, 8, 0);
    lv_obj_set_style_pad_top(s_tabContent, 4, 0);
    lv_obj_set_style_pad_bottom(s_tabContent, 8, 0);
    lv_obj_set_style_pad_row(s_tabContent, 8, 0);
    lv_obj_set_flex_flow(s_tabContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_tabContent, LV_SCROLLBAR_MODE_AUTO);

    updateTabBarStyle();
    renderActiveTab();
}

void refreshAppStoreTheme() {
    if (!s_storeBody) return;
    updateTabBarStyle();
    renderActiveTab();
}

} // namespace

void appStoreOpenTab(AppStoreTab tab) {
    s_activeTab = tab;
    if (s_storeBody) {
        updateTabBarStyle();
        renderActiveTab();
    } else {
        appManagerLaunchById("app_store");
    }
}

void appStoreRefreshCurrentTab() {
    if (!s_storeBody) return;
    renderActiveTab();
}

void appStoreNotifyAppsChanged() {
    if (!s_storeBody || s_asyncRefreshPending) return;
    s_asyncRefreshPending = true;
    lv_async_call(asyncRefreshTabCb, nullptr);
}

bool appStoreIsOpen() {
    return s_storeBody != nullptr;
}

REGISTER_APP_THEMED(app_store, "app_store", "App Store", LV_SYMBOL_DOWNLOAD,
                    M3Color::PRIMARY, 0x1A73E8,
                    buildAppStore, nullptr, refreshAppStoreTheme, 0);

#endif // !APP_STORE_HOST_TEST
