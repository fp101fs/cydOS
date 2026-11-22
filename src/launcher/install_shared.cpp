#include "install_shared.h"
#include "ble_bonds.h"
#include "launcher_app_registry.h"
#include "launcher_ui_bridge.h"
#include <cstdio>

const char *launcherInstallDefaultDataLabel(uint8_t subtype) { return subtype == 0x81 ? "vfs" : "spiffs"; }

String launcherGenerateAppNum(const String &sourcePath) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < sourcePath.length(); ++i) {
        crc ^= static_cast<uint8_t>(sourcePath[i]);
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u * (crc & 1u));
        }
    }
    crc ^= 0xFFFFFFFF;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", static_cast<unsigned long>(crc));
    return String(buf);
}

String launcherInstallAppDisplayName(const String &sourceName, const String &fallbackName) {
    if (!fallbackName.isEmpty()) return fallbackName;
    return launcherAppNameFromFile(sourceName);
}

String launcherInstallNextAppLabel(
    const LauncherPartitionTable &table, const String &sourceName, const String &preferredName,
    const char *emptyFallback
) {
    String labelSource = launcherInstallAppDisplayName(sourceName, preferredName);
    if (labelSource.isEmpty() && emptyFallback != nullptr) labelSource = emptyFallback;
    return launcherPartitionNextAppLabel(table, labelSource);
}

void launcherSaveInstalledAppMetadata(
    const LauncherPartitionTable &table, const LauncherPartitionEntry &appEntry, const String &sourceName,
    const String &preferredName, const std::vector<String> &fatLabels, const String &spiffsLabel
) {
    if (appEntry.offset == 0) {
        g_launcherLastInstalledApp = launcherInstallAppDisplayName(sourceName, preferredName);
        if (g_launcherLastInstalledApp.isEmpty()) g_launcherLastInstalledApp = "WebUI App";
        launcherUiSaveState();
        return;
    }

    String installedLabel = String(appEntry.label);
    launcherPruneAppRegistry(table);
    launcherBleBondsSwitchTo(installedLabel.c_str());

    LauncherAppMetadata metadata;
    metadata.name = launcherInstallAppDisplayName(sourceName, preferredName);
    if (metadata.name.isEmpty()) metadata.name = installedLabel;
    metadata.label = installedLabel;
    metadata.fatLabels = fatLabels;
    metadata.spiffsLabel = spiffsLabel;
    metadata.appNum = launcherGenerateAppNum(sourceName);
    launcherSaveAppMetadata(metadata);

    g_launcherLastInstalledApp = metadata.name;
    launcherUiSaveState();
    launcherUiNotifyAppsChanged(installedLabel.c_str());
}
