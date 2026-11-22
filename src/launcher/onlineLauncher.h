#ifndef LAUNCHER_ONLINE_LAUNCHER_H
#define LAUNCHER_ONLINE_LAUNCHER_H

#include "partition_install_layout.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Vendored from bmorcelli/Launcher (src/onlineLauncher.h), wired to pixel-cyd's
// wifi_mgr, VSPI SD functions, and Material 3 launcher_ui_bridge.

extern JsonDocument g_launcherHubDoc;
extern int g_launcherTotalFirmware;
extern int g_launcherNumPages;
extern uint8_t g_launcherCurrentPage;
extern String g_launcherOtaTag;
extern String g_launcherDwnPath;

// Load persisted LauncherHub settings (e.g., device OTA tag) from NVS "l_cfg".
void launcherOnlineInit();
void launcherSetOtaTag(const String &tag);
const String &launcherGetOtaTag();

// PSRAM-first / internal-RAM fallback ArduinoJson allocator & list filter.
ArduinoJson::Allocator *launcherJsonAllocator();
void buildFirmwareListFilter(JsonDocument &filter);

// Pure string/protocol helpers (also host-tested).
String launcherEncodeQueryValue(const String &value);
String launcherSanitizeFilename(String input);
bool launcherParseContentRangeTotal(const char *contentRange, size_t &total);
String launcherBuildSourceUrl(const String &fid, const String &sourceUrl, bool useProxy);
uint32_t launcherFirstDataPartitionOffsetFromTable(const uint8_t *table, size_t len);

// Wi-Fi helpers wired to pixel-cyd's wifi_mgr.
bool launcherIsWifiConnected();
bool ensureWifiConnected();

// LauncherHub catalog & version APIs.
bool GetJsonFromLauncherHub(
    uint8_t page = 1, const String &order = "downloads", bool star = false, const String &query = "",
    String *errorOut = nullptr
);
JsonDocument getVersionInfo(const String &fid, String *errorOut = nullptr);

// OTA App install & SD download flows.
bool installFirmwareFromManifest(
    const String &fid, const String &version, String installedName = "", bool rebootAfter = false
);
bool installFirmware(
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool nb,
    std::vector<LauncherInstallDataPartition> &dataPartitions, String installedName = "",
    bool askSpiffs = true, bool rebootAfter = false
);
bool installExtFirmware(const String &url, bool rebootAfter = false);

bool downloadFirmware(
    const String &fid, String file, String fileName, String folder = "/downloads/",
    const String &version = ""
);
void saveDownloadedFirmware(const String &folder, const String &fid, const String &version);
bool checkForUpdates(String *errorOut = nullptr);

bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label);

#endif
