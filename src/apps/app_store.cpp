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
#include "../components/m3_widgets.h"
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

void renderActiveTab();
void updateTabBarStyle();

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
    if (next == s_activeTab) return;
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

void buildExplorePlaceholderTab(lv_obj_t* container) {
    lv_obj_t* card = m3Card(container, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  Explore LauncherHub");
    lv_obj_set_style_text_color(title, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    char statusBuf[96];
    if (g_sys.wifiConnected) {
        snprintf(statusBuf, sizeof(statusBuf), "Wi-Fi: %s  •  Tag: %s",
                 g_sys.wifiSsid[0] ? g_sys.wifiSsid : "Connected",
                 g_launcherOtaTag.c_str());
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "Wi-Fi offline  •  Tag: %s",
                 g_launcherOtaTag.c_str());
    }

    lv_obj_t* sub = lv_label_create(card);
    lv_label_set_text(sub, statusBuf);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

    lv_obj_t* hint = lv_label_create(card);
    lv_label_set_text(hint, "Online App catalog, search, version picker, and OTA install are wired in Items 7 & 8.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);

    if (!g_sys.wifiConnected) {
        lv_obj_t* wifiBtn = m3PillBtn(card, LV_SYMBOL_WIFI "  Connect Wi-Fi",
                                      M3Color::PRIMARY_CONT, M3Color::PRIMARY, LV_PCT(100), 32);
        lv_obj_add_event_cb(wifiBtn, openWifiSettingsCb, LV_EVENT_CLICKED, nullptr);
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
            buildExplorePlaceholderTab(s_tabContent);
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
