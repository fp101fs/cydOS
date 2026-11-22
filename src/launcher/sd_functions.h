#ifndef LAUNCHER_SD_FUNCTIONS_H
#define LAUNCHER_SD_FUNCTIONS_H

#include "idf_update.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// Vendored from bmorcelli/Launcher (src/sd_functions.h), wired to pixel-cyd's
// VSPI SD card config (src/config/hw_config.h) and launcher_ui_bridge.

extern SPIClass g_launcherSdSpi;
extern bool g_launcherSdMounted;

struct LauncherSdEntry {
    String name;
    String fullPath;
    bool isDir = false;
    size_t size = 0;
};

bool setupSdCard();
void unmountSdCard();
bool isSdCardMounted();

bool listSdDirectory(
    const String &folder, std::vector<LauncherSdEntry> &entries, bool onlyBins = false,
    bool hideDotFiles = true
);

bool deleteFromSd(const String &path);
bool renameOnSd(const String &path, const String &newName);
bool createFolderOnSd(const String &parentFolder, const String &folderName);

bool copyFile(const String &path);
bool hasFileToCopy();
String getFileToCopy();
void clearFileToCopy();
bool pasteFile(const String &destFolder);

// Analyze a .bin file on SD (raw app or merged image with partition table at 0x8000)
// and install it into dynamic flash slots.
bool updateFromSD(const String &path, bool askSpiffs = true, bool rebootAfter = false);

// Stream a raw data image into an existing DATA partition by label.
bool performDATAUpdate(Stream &updateSource, size_t updateSize, const char *label);

#endif
