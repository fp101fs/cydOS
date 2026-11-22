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
#include "../launcher/sd_functions.h"
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

// Reusable m3_keyboard text prompt modal (Search Apps / Install from URL)
lv_obj_t* s_promptBackdrop = nullptr;
lv_obj_t* s_promptTa = nullptr;
std::function<void(const char*)> s_promptOnSubmit;

void renderActiveTab();
void updateTabBarStyle();
void queueExploreAction(PendingExploreAction action, size_t itemIdx = 0, const char* url = nullptr);

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

void buildSdCardPlaceholderTab(lv_obj_t* container) {
    lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_SD_CARD "  SD Card Installer");
    lv_obj_set_style_text_color(title, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    lv_obj_t* sub = lv_label_create(card);
    lv_label_set_text(sub, isSdCardMounted()
                               ? "SD card mounted. Ready for /downloads/ browser and .bin flashing."
                               : "SD card not mounted. Insert a FAT32 SD card with .bin Apps.");
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

    lv_obj_t* checkBtn = m3PillBtn(card, LV_SYMBOL_REFRESH "  Check SD Card",
                                   M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 32);
    lv_obj_add_event_cb(checkBtn, checkSdBtnCb, LV_EVENT_CLICKED, nullptr);
}

void buildToolsPlaceholderTab(lv_obj_t* container) {
    AppStoreStorageSummary storage = computeLiveStorageSummary(nullptr, appGetDynamicCount());

    lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  App Store Tools");
    lv_obj_set_style_text_color(title, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    char poolStr[20], usedStr[20], freeStr[20];
    appStoreFormatSizeCompact(storage.dynamicTotalBytes, poolStr, sizeof(poolStr));
    appStoreFormatSizeCompact(storage.usedBytes, usedStr, sizeof(usedStr));
    appStoreFormatSizeCompact(storage.freeBytes, freeStr, sizeof(freeStr));

    m3InfoRow(card, "Dynamic Pool", poolStr);
    m3InfoRow(card, "Used / Free", (String(usedStr) + " / " + freeStr).c_str());
    m3InfoRow(card, "Device Tag", g_launcherOtaTag.c_str());

    lv_obj_t* syncBtn = m3PillBtn(card, LV_SYMBOL_REFRESH "  Sync Home Screen",
                                  M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 32);
    lv_obj_add_event_cb(syncBtn, syncRefreshBtnCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* roadCard = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 14);
    lv_obj_set_flex_flow(roadCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(roadCard, 3, 0);

    lv_obj_t* rTitle = lv_label_create(roadCard);
    lv_label_set_text(rTitle, "Coming in Tools");
    lv_obj_set_style_text_color(rTitle, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(rTitle, &lv_font_montserrat_12, 0);

    lv_obj_t* rDesc = lv_label_create(roadCard);
    lv_label_set_text(rDesc, "• Storage & Partitions (PMan)\n• Backup / Restore Manager\n• Web Upload (WUI) & Store Settings");
    lv_obj_set_width(rDesc, LV_PCT(100));
    lv_label_set_long_mode(rDesc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(rDesc, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(rDesc, &lv_font_montserrat_10, 0);
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
    lv_obj_clean(s_tabContent);
    lv_obj_scroll_to_y(s_tabContent, 0, LV_ANIM_OFF);

    switch (s_activeTab) {
        case AppStoreTab::Explore:
            buildExploreTab(s_tabContent);
            break;
        case AppStoreTab::SdCard:
            buildSdCardPlaceholderTab(s_tabContent);
            break;
        case AppStoreTab::Installed:
            buildInstalledTab(s_tabContent);
            break;
        case AppStoreTab::Tools:
            buildToolsPlaceholderTab(s_tabContent);
            break;
        default:
            buildInstalledTab(s_tabContent);
            break;
    }
}

void storeBodyDeleteCb(lv_event_t* e) {
    (void)e;
    closeTextPromptModal();
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
