#ifndef LAUNCHER_APP_REGISTRY_H
#define LAUNCHER_APP_REGISTRY_H

#include "partition_table_model.h"
#include <Arduino.h>
#include <vector>

// Vendored from bmorcelli/Launcher (app_registry.h), renamed cleanly so it does not
// collide with pixel-cyd's src/apps/app_registry.h. Uses NVS namespace "l_apps".
struct LauncherAppMetadata {
    String name;
    String label;
    std::vector<String> fatLabels;
    String spiffsLabel;
    String appNum; // CRC32 8-char hash of source filepath/URL (e.g. "a1b2c3d4")
};

std::vector<LauncherAppMetadata> launcherLoadAppRegistry();
std::vector<LauncherAppMetadata> launcherListInstalledApps();
bool launcherSaveAppMetadata(const LauncherAppMetadata &app);
bool launcherRemoveAppMetadata(const char *label);
bool launcherPruneAppRegistry(const LauncherPartitionTable &table);
std::vector<String> launcherAppFatLabelsForLabel(const char *label);
String launcherAppSpiffsLabelForLabel(const char *label);
String launcherAppNumForLabel(const char *label);
String launcherAppDisplayNameForLabel(const char *label);
String launcherSelectedBootAppName();
bool launcherBootCurrentApp();
bool launcherBootAppByLabel(const char *label);
bool launcherDeleteAppByLabel(const char *label, bool skipConfirm = false, bool rebootAfter = true);
bool launcherRenameAppByLabel(const char *label, const String &newName);
String launcherAppNameFromFile(const String &source);
bool launcherClearAppRegistry();

#endif
