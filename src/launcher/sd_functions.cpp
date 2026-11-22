#include "sd_functions.h"
#include "backup_manager.h"
#include "install_shared.h"
#include "launcher_platform.h"
#include "launcher_ui_bridge.h"
#include "littlefs_patch.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "../config/hw_config.h"
#include <algorithm>
#include <cstring>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <esp_partition.h>
#include <memory>

SPIClass g_launcherSdSpi(VSPI);
bool g_launcherSdMounted = false;

namespace {
String s_fileToCopy;

bool sortSdEntries(const LauncherSdEntry &a, const LauncherSdEntry &b) {
    if (a.isDir != b.isDir) return a.isDir > b.isDir;
    String fa = a.name;
    fa.toUpperCase();
    String fb = b.name;
    fb.toUpperCase();
    return fa < fb;
}

void dataUpdateProgressBridge(size_t current, size_t total) {
    launcherUiProgressBytes(current, total);
}

String installedAppNameFromPath(const String &path) {
    return launcherInstallAppDisplayName(path);
}

bool readSdBytes(File &file, uint32_t offset, void *buffer, size_t len) {
    if (!file.seek(offset)) return false;
    return file.readBytes(reinterpret_cast<char *>(buffer), len) == len;
}

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

String readPartitionLabel(const uint8_t *entry) {
    char label[17] = {0};
    memcpy(label, entry + 12, 16);
    label[16] = '\0';
    return String(label);
}

uint32_t boundedSdPartitionPayload(File &file, uint32_t offset, uint32_t declaredSize, uint32_t maxSize) {
    if (offset == 0 || file.size() <= offset || declaredSize == 0) return 0;
    uint32_t availableSize = file.size() - offset;
    return launcherPartitionBoundedPayloadSize(declaredSize, 0, maxSize, availableSize);
}

bool sdPartitionIsEmpty(File &file, uint32_t offset) {
    if (offset == 0 || file.size() <= offset) return true;
    uint8_t buffer[16];
    uint32_t len = std::min<uint32_t>(sizeof(buffer), file.size() - offset);
    if (!readSdBytes(file, offset, buffer, len)) return true;
    bool allFF = true;
    bool allZero = true;
    for (uint32_t i = 0; i < len; ++i) {
        if (buffer[i] != 0xFF) allFF = false;
        if (buffer[i] != 0x00) allZero = false;
    }
    return allFF || allZero;
}

bool measureSdEspImage(File &file, uint32_t imageOffset, uint32_t &imageSize) {
    esp_image_header_t header;
    if (!readSdBytes(file, imageOffset, &header, sizeof(header))) return false;
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        return false;
    }

    uint32_t cursor = imageOffset + sizeof(header);
    const uint32_t fileSize = file.size();
    if (cursor > fileSize) return false;

    for (uint8_t i = 0; i < header.segment_count; ++i) {
        uint8_t segmentHeader[sizeof(esp_image_segment_header_t)];
        if (!readSdBytes(file, cursor, segmentHeader, sizeof(segmentHeader))) return false;
        const uint32_t segmentSize = readLe32(segmentHeader + 4);
        cursor += sizeof(segmentHeader);
        if (segmentSize > fileSize || cursor > fileSize - segmentSize) return false;
        cursor += segmentSize;
    }

    uint32_t end = launcherAlignUp(cursor, 16) + 1;
    if (header.hash_appended) end += ESP_IMAGE_HASH_LEN;
    end = launcherAlignUp(end, 16);
    if (end <= imageOffset || end > fileSize) return false;

    imageSize = end - imageOffset;
    return true;
}

uint32_t effectiveSdAppSize(File &file, uint32_t appOffset, uint32_t fallbackSize) {
    uint32_t measuredSize = 0;
    if (measureSdEspImage(file, appOffset, measuredSize)) {
        if (fallbackSize == 0 || measuredSize < fallbackSize) {
            launcherConsolePrintf(
                "Measured SD app image at 0x%06X: 0x%06X (%u bytes), fallback was 0x%06X\n",
                appOffset,
                measuredSize,
                measuredSize,
                fallbackSize
            );
            return measuredSize;
        }
    }
    return fallbackSize;
}

bool flashRawFromSd(
    File &file, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target, bool appImage,
    const char *stepText
) {
    if (!file.seek(sourceOffset)) {
        launcherConsolePrintf(
            "flashRawFromSd: initial seek to 0x%08X failed\n", static_cast<unsigned>(sourceOffset)
        );
        return false;
    }
    launcherUiProgressBytes(0, imageSize, stepText);
    if (!launcherRawUpdateBegin(target.offset, target.size, imageSize, appImage)) {
        launcherConsolePrintf(
            "flashRawFromSd: launcherRawUpdateBegin failed target=0x%08X/0x%08X image=0x%08X (%s)\n",
            static_cast<unsigned>(target.offset),
            static_cast<unsigned>(target.size),
            static_cast<unsigned>(imageSize),
            launcherUpdateLastErrorName()
        );
        return false;
    }

    constexpr size_t bufferSize = 4096;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[bufferSize]);
    if (!buf) {
        launcherRawUpdateEnd();
        return false;
    }

    size_t written = 0;
    while (written < imageSize) {
        size_t toRead = min(bufferSize, imageSize - written);
        int bytesRead = file.readBytes(reinterpret_cast<char *>(buf.get()), toRead);
        if (bytesRead <= 0) {
            launcherDelayMs(20);
            if (!file.seek(sourceOffset + written)) {
                launcherConsolePrintf(
                    "SD read failed at offset 0x%08X (re-seek failed)\n",
                    static_cast<unsigned>(sourceOffset + written)
                );
                launcherRawUpdateEnd();
                return false;
            }
            bytesRead = file.readBytes(reinterpret_cast<char *>(buf.get()), toRead);
        }
        if (bytesRead <= 0) {
            launcherConsolePrintf(
                "SD read failed at offset 0x%08X\n", static_cast<unsigned>(sourceOffset + written)
            );
            launcherRawUpdateEnd();
            return false;
        }
        if (launcherRawUpdateWrite(buf.get(), bytesRead) != static_cast<size_t>(bytesRead)) {
            launcherConsolePrintf(
                "Flash write failed at partition offset 0x%08X (%s)\n",
                static_cast<unsigned>(written),
                launcherUpdateLastErrorName()
            );
            launcherRawUpdateEnd();
            return false;
        }
        written += bytesRead;
        launcherUiProgressBytes(written, imageSize);
        launcherDelayMs(1);
    }

    if (!launcherRawUpdateEnd()) {
        launcherConsolePrintf(
            "flashRawFromSd: launcherRawUpdateEnd failed (%s)\n", launcherUpdateLastErrorName()
        );
        return false;
    }

    if (!appImage) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(target, &patchError)) {
            launcherConsolePrintf(
                "LittleFS patch failed after SD copy label=%s offset=0x%08X size=0x%08X: %s\n",
                target.label,
                target.offset,
                target.size,
                patchError.c_str()
            );
            return false;
        }
    }

    return true;
}

bool installFromSdDynamic(
    File &file, const String &path, uint32_t appSize, uint32_t appOffset,
    std::vector<LauncherInstallDataPartition> &dataPartitions, String &installedAppNameOut
) {
    String error;
    LauncherPartitionTable table;
    launcherUiStep("Reading partition table...", -1);
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherUiError(error.length() ? error : "Could not read partition table");
        return false;
    }

    if (appSize == 0 || appOffset + appSize > file.size()) {
        launcherUiError("Invalid app binary");
        return false;
    }

    String displayName = installedAppNameFromPath(path);
    installedAppNameOut = displayName;
    String appLabel = launcherPartitionNextAppLabel(table, displayName);
    LauncherPartitionEntry appEntry;

    launcherUiStep("Planning storage layout...", -1);
    if (!launcherSelectInstallLayout(table, appSize, appLabel, dataPartitions, appEntry, error)) {
        launcherConsolePrintf("SD install layout failed: %s\n", error.c_str());
        launcherUiError(error.length() ? error : "Not enough flash space");
        return false;
    }
    if (!launcherPartitionValidate(table, &error)) {
        launcherUiError(error.length() ? error : "Invalid partition layout");
        return false;
    }

    String appNum = generateAppNum(path);
    bool shouldRestore = false;
    BackupInstallInfo prevInstall = loadInstalledFromConfig(appNum);
    if (!prevInstall.appNum.isEmpty()) {
        bool hasRestorable = false;
        for (const auto &bp : prevInstall.partitions) {
            if (!bp.lastBackupPath.isEmpty() && SD.exists(bp.lastBackupPath)) {
                hasRestorable = true;
                break;
            }
        }
        if (hasRestorable) {
            if (g_launcherAutoBackup) {
                shouldRestore = launcherUiConfirm(
                    "Restore Backup?",
                    "A previous data backup was found for this app. Restore it?",
                    "Restore"
                );
            } else {
                shouldRestore = true;
            }
        }
    }

    launcherUiBeginProgress("Installing App", "Flashing app firmware...");
    if (!flashRawFromSd(file, appOffset, appSize, appEntry, true, "Flashing app firmware...")) {
        launcherUiError(String("App flash failed: ") + launcherUpdateLastErrorName());
        return false;
    }

    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry || dp.copySize == 0) continue;
        if (shouldRestore) {
            bool willRestore = false;
            for (const auto &bp : prevInstall.partitions) {
                if (bp.label == dp.label && !bp.lastBackupPath.isEmpty() && SD.exists(bp.lastBackupPath)) {
                    willRestore = true;
                    break;
                }
            }
            if (willRestore) continue;
        }
        const char *typeStr = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
        String stepText = String("Flashing ") + typeStr + " data...";
        const uint32_t copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        if (!flashRawFromSd(file, dp.sourceOffset, copySize, dp.entry, false, stepText.c_str())) {
            launcherUiError(String(typeStr) + " flash failed: " + launcherUpdateLastErrorName());
            return false;
        }
    }

    launcherUiStep("Updating partition table...", -1);
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        launcherUiError(error.length() ? error : "Failed to write partition table");
        return false;
    }

    launcherUiStep("Configuring boot slot...", -1);
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        launcherUiError(error.length() ? error : "Failed to set boot slot");
        return false;
    }

    if (shouldRestore) {
        for (const auto &bp : prevInstall.partitions) {
            if (!bp.lastBackupPath.isEmpty() && SD.exists(bp.lastBackupPath)) {
                bool restored = false;
                for (const auto &dp : dataPartitions) {
                    if (dp.label == bp.label && dp.hasEntry) {
                        restored = restorePartitionFromBackupDirect(
                            bp.label.c_str(), bp.lastBackupPath.c_str(), dp.entry.offset, dp.entry.size
                        );
                        break;
                    }
                }
                if (!restored) {
                    restored = restorePartitionFromBackup(bp.label.c_str(), bp.lastBackupPath.c_str());
                }
                if (!restored) {
                    launcherConsolePrintf("[Restore] Failed: %s\n", bp.label.c_str());
                }
            }
        }
    }

    launcherUiStep("Adding app to Home Screen...", 100);
    {
        std::vector<String> fatLabels;
        String registeredSpiffsLabel;
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            if (dp.subtype == 0x81) fatLabels.push_back(dp.label);
            else registeredSpiffsLabel = dp.label;
        }
        launcherSaveInstalledAppMetadata(
            table, appEntry, path, displayName, fatLabels, registeredSpiffsLabel
        );
        launcherUiSaveState();

        BackupInstallInfo bkInfo;
        bkInfo.appNum = appNum;
        bkInfo.sdFilepath = path;
        bkInfo.appName = displayName;
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            BackupPartitionInfo part;
            part.label = dp.label;
            part.type = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
            for (const auto &bp : prevInstall.partitions) {
                if (bp.label == part.label && !bp.lastBackupPath.isEmpty()) {
                    part.lastBackupPath = bp.lastBackupPath;
                    break;
                }
            }
            bkInfo.partitions.push_back(part);
        }
        saveInstalledToConfig(bkInfo);
    }

    return true;
}
} // namespace

bool setupSdCard() {
    if (g_launcherSdMounted && SD.cardType() != CARD_NONE) {
        return true;
    }

    g_launcherSdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    launcherDelayMs(10);
    if (!SD.begin(PIN_SD_CS, g_launcherSdSpi)) {
        launcherConsolePrintln("Failed to mount SD card");
        g_launcherSdMounted = false;
        return false;
    }
    launcherConsolePrintln("SD card mounted");
    g_launcherSdMounted = true;
    return true;
}

void unmountSdCard() {
    if (g_launcherSdMounted) {
        SD.end();
        g_launcherSdMounted = false;
    }
}

bool isSdCardMounted() {
    return g_launcherSdMounted && SD.cardType() != CARD_NONE;
}

bool listSdDirectory(
    const String &folder, std::vector<LauncherSdEntry> &entries, bool onlyBins, bool hideDotFiles
) {
    entries.clear();
    if (!setupSdCard()) return false;

    String dirPath = folder.isEmpty() ? String("/") : folder;
    File root = SD.open(dirPath);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        SD.end();
        g_launcherSdMounted = false;
        return false;
    }

    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        bool isDir = f.isDirectory();
        size_t fileSize = isDir ? 0 : f.size();
        String fullPath = String(f.path());
        if (fullPath.isEmpty()) {
            String rawName = String(f.name());
            if (rawName.startsWith("/")) fullPath = rawName;
            else fullPath = (dirPath == "/" ? String("/") : dirPath + "/") + rawName;
        }
        f.close();

        String nameOnly = fullPath.substring(fullPath.lastIndexOf('/') + 1);
        if (nameOnly.isEmpty()) continue;
        if (hideDotFiles && nameOnly.startsWith(".")) continue;

        if (!isDir && onlyBins) {
            int dotIndex = nameOnly.lastIndexOf('.');
            String ext = dotIndex >= 0 ? nameOnly.substring(dotIndex + 1) : "";
            ext.toUpperCase();
            if (ext != "BIN") continue;
        }

        LauncherSdEntry entry;
        entry.name = nameOnly;
        entry.fullPath = fullPath;
        entry.isDir = isDir;
        entry.size = fileSize;
        entries.push_back(entry);
    }
    root.close();

    std::sort(entries.begin(), entries.end(), sortSdEntries);
    return true;
}

bool deleteFromSd(const String &path) {
    if (!setupSdCard() || path.isEmpty() || path == "/") return false;
    File dir = SD.open(path);
    if (!dir) return false;
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path.c_str());
    }

    dir.rewindDirectory();
    bool success = true;
    bool isDir = false;
    String fullPath = dir.getNextFileName(&isDir);
    while (!fullPath.isEmpty()) {
        if (isDir) {
            success &= deleteFromSd(fullPath);
        } else {
            success &= SD.remove(fullPath.c_str());
        }
        yield();
        fullPath = dir.getNextFileName(&isDir);
    }
    dir.close();
    success &= SD.rmdir(path.c_str());
    return success;
}

bool renameOnSd(const String &path, const String &newName) {
    String trimmed = newName;
    trimmed.trim();
    if (trimmed.isEmpty() || trimmed.indexOf('/') >= 0 || path.isEmpty()) return false;
    if (!setupSdCard()) return false;

    int slash = path.lastIndexOf('/');
    String parent = slash > 0 ? path.substring(0, slash) : String("");
    String dest = parent + "/" + trimmed;
    if (dest == path) return false;
    return SD.rename(path, dest);
}

bool createFolderOnSd(const String &parentFolder, const String &folderName) {
    String trimmed = folderName;
    trimmed.trim();
    if (trimmed.isEmpty() || trimmed.indexOf('/') >= 0) return false;
    if (!setupSdCard()) return false;

    String base = parentFolder.isEmpty() ? String("/") : parentFolder;
    if (!base.endsWith("/")) base += "/";
    return SD.mkdir(base + trimmed);
}

bool copyFile(const String &path) {
    if (!setupSdCard() || path.isEmpty()) return false;
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    if (file.isDirectory()) {
        file.close();
        launcherUiError("Cannot copy a folder");
        return false;
    }
    file.close();
    s_fileToCopy = path;
    return true;
}

bool hasFileToCopy() { return !s_fileToCopy.isEmpty(); }
String getFileToCopy() { return s_fileToCopy; }
void clearFileToCopy() { s_fileToCopy = ""; }

bool pasteFile(const String &destFolder) {
    if (s_fileToCopy.isEmpty() || !setupSdCard()) return false;

    File sourceFile = SD.open(s_fileToCopy, FILE_READ);
    if (!sourceFile) {
        launcherUiError("Could not open source file");
        return false;
    }

    String base = destFolder.isEmpty() ? String("/") : destFolder;
    if (base.endsWith("/") && base.length() > 1) base.remove(base.length() - 1);
    String filename = s_fileToCopy.substring(s_fileToCopy.lastIndexOf('/') + 1);
    String destPath = (base == "/" ? String("") : base) + "/" + filename;

    File destFile = SD.open(destPath, FILE_WRITE, true);
    if (!destFile) {
        sourceFile.close();
        launcherUiError("Could not create destination file");
        return false;
    }

    constexpr size_t bufferSize = 4096;
    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[bufferSize]);
    if (!buffer) {
        sourceFile.close();
        destFile.close();
        launcherUiError("Out of memory while copying");
        return false;
    }

    size_t total = sourceFile.size();
    size_t done = 0;
    launcherUiBeginProgress("Copy File", ("Copying " + filename + "...").c_str());
    launcherUiProgressBytes(0, total);

    size_t bytesRead = 0;
    while ((bytesRead = sourceFile.read(buffer.get(), bufferSize)) > 0) {
        if (destFile.write(buffer.get(), bytesRead) != bytesRead) {
            sourceFile.close();
            destFile.close();
            SD.remove(destPath);
            launcherUiError("Failed writing to SD card");
            return false;
        }
        done += bytesRead;
        launcherUiProgressBytes(done, total);
    }

    sourceFile.close();
    destFile.close();
    launcherUiFinish(true, "File Copied", destPath.c_str());
    return true;
}

bool updateFromSD(const String &path, bool askSpiffs, bool rebootAfter) {
    if (!setupSdCard()) {
        launcherUiError("SD card not found");
        return false;
    }

    uint8_t partitionEntry[LAUNCHER_PARTITION_ENTRY_SIZE];
    uint32_t appSize = 0;
    uint32_t appOffset = 0;
    std::vector<LauncherInstallDataPartition> dataPartitions;

    File file = SD.open(path, FILE_READ);
    if (!file) {
        launcherUiError("Could not open firmware file");
        return false;
    }

    launcherUiBeginProgress("Installing App", "Inspecting firmware binary...");

    bool isMerged = false;
    if (file.size() >= LAUNCHER_PARTITION_TABLE_OFFSET + 16 && file.seek(LAUNCHER_PARTITION_TABLE_OFFSET)) {
        if (file.read(partitionEntry, 16) == 16 &&
            partitionEntry[0] == 0xAA && partitionEntry[1] == 0x50 && partitionEntry[2] == 0x01) {
            isMerged = true;
        }
    }

    String installedName;
    if (!isMerged) {
        appSize = effectiveSdAppSize(file, 0, file.size());
        bool ok = installFromSdDynamic(file, path, appSize, 0, dataPartitions, installedName);
        file.close();
        if (!ok) return false;
        if (rebootAfter) {
            launcherUiStep("Restarting into app...", 100);
            launcherUiReboot();
        } else {
            launcherUiFinish(true, "App Installed", (installedName + " added to Home Screen").c_str());
        }
        return true;
    }

    for (int i = 0; i < LAUNCHER_PARTITION_TABLE_SIZE; i += LAUNCHER_PARTITION_ENTRY_SIZE) {
        if (!file.seek(LAUNCHER_PARTITION_TABLE_OFFSET + i)) break;
        if (file.read(partitionEntry, LAUNCHER_PARTITION_ENTRY_SIZE) != LAUNCHER_PARTITION_ENTRY_SIZE) break;
        if (partitionEntry[0] == 0xEB && partitionEntry[1] == 0xEB) break;
        if (partitionEntry[0] == 0xFF && partitionEntry[1] == 0xFF) break;

        if (partitionEntry[0x02] == 0x00 &&
            (partitionEntry[0x03] == 0x00 || partitionEntry[0x03] == 0x10 || partitionEntry[0x03] == 0x20) &&
            appSize == 0) {
            uint32_t declaredAppSize = readLe32(partitionEntry + 0x08);
            appOffset = readLe32(partitionEntry + 0x04);
            if (file.size() < (declaredAppSize + appOffset)) {
                appSize = file.size() > appOffset ? file.size() - appOffset : 0;
            } else {
                appSize = effectiveSdAppSize(file, appOffset, declaredAppSize);
            }
        }

        if (partitionEntry[0x02] == 0x01 && (partitionEntry[3] == 0x82 || partitionEntry[3] == 0x83)) {
            LauncherInstallDataPartition dp;
            dp.subtype = partitionEntry[3];
            dp.sourceOffset = readLe32(partitionEntry + 0x04);
            const uint32_t declaredSize = readLe32(partitionEntry + 0x08);
            String declaredLabel = readPartitionLabel(partitionEntry);
            dp.label = declaredLabel.isEmpty() ? "spiffs" : declaredLabel;
            const bool partitionEmpty = sdPartitionIsEmpty(file, dp.sourceOffset);

            if (partitionEmpty && declaredSize <= LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            } else if (dp.label != "spiffs" && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
                dp.partitionSize = declaredSize;
            } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE;
            } else {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            }

            dp.copySize = partitionEmpty
                              ? 0
                              : boundedSdPartitionPayload(
                                    file,
                                    dp.sourceOffset,
                                    declaredSize,
                                    dp.partitionSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE
                                        ? declaredSize
                                        : dp.partitionSize
                                );
            if (file.size() < dp.sourceOffset) dp.copySize = 0;
            dataPartitions.push_back(dp);
        }

        if (partitionEntry[0x02] == 0x01 && partitionEntry[3] == 0x81) {
            LauncherInstallDataPartition dp;
            dp.subtype = 0x81;
            dp.label = readPartitionLabel(partitionEntry);
            uint32_t declaredSize = readLe32(partitionEntry + 0x08);
            dp.sourceOffset = readLe32(partitionEntry + 0x04);
            uint32_t availableSize =
                dp.sourceOffset != 0 && file.size() > dp.sourceOffset ? file.size() - dp.sourceOffset : 0;
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, 0, availableSize);
            dp.partitionSize = payload.partitionSize;
            dp.copySize = payload.copySize;
            dataPartitions.push_back(dp);
        }
    }

    auto spiffsIt = std::find_if(
        dataPartitions.begin(), dataPartitions.end(), [](const LauncherInstallDataPartition &d) {
            return d.subtype != 0x81 && d.label == "spiffs";
        }
    );
    if (spiffsIt != dataPartitions.end()) {
        if (!askSpiffs) {
            spiffsIt->copySize = 0;
        } else if (spiffsIt->copySize > 0) {
            bool copySpiffs = launcherUiConfirm(
                "Include App Data?",
                "This firmware includes a SPIFFS data image. Flash it as well?",
                "Include"
            );
            if (!copySpiffs) spiffsIt->copySize = 0;
            launcherUiBeginProgress("Installing App", "Preparing installation...");
        }
    }

    bool ok = installFromSdDynamic(file, path, appSize, appOffset, dataPartitions, installedName);
    file.close();
    if (!ok) return false;

    if (rebootAfter) {
        launcherUiStep("Restarting into app...", 100);
        launcherUiReboot();
    } else {
        launcherUiFinish(true, "App Installed", (installedName + " added to Home Screen").c_str());
    }
    return true;
}

bool performDATAUpdate(Stream &updateSource, size_t updateSize, const char *label) {
    launcherUiBeginProgress("Updating Data", (String("Writing ") + label + "...").c_str());
    launcherUiProgressBytes(0, updateSize);

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition) {
        launcherUiError(String("Data partition not found: ") + label);
        return false;
    }

    if (!launcherRawUpdateStream(
            updateSource, partition->address, partition->size, updateSize, false, dataUpdateProgressBridge
        )) {
        launcherUiError(String("Failed updating ") + label);
        return false;
    }

    String patchError;
    if (!launcherPatchReducedLittlefsSuperblocks(partition->address, partition->size, &patchError)) {
        launcherConsolePrintf(
            "LittleFS patch failed after DATA restore label=%s offset=0x%08X size=0x%08X: %s\n",
            label,
            partition->address,
            partition->size,
            patchError.c_str()
        );
        launcherUiError("LittleFS patch failed");
        return false;
    }

    launcherUiFinish(true, "Data Updated", label);
    return true;
}
