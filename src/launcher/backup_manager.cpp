#include "backup_manager.h"
#include "install_shared.h"
#include "launcher_platform.h"
#include "launcher_ui_bridge.h"
#include "littlefs_patch.h"
#include "partition_table_model.h"
#include "sd_functions.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <cctype>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <memory>

#define BACKUP_DATA_PATH "/bkp/backupData.json"
static constexpr size_t kBackupBufferSize = 4096;

namespace {
struct HeapCapsDeleter {
    void operator()(uint8_t *ptr) const {
        if (ptr) heap_caps_free(ptr);
    }
};

using HeapBuffer = std::unique_ptr<uint8_t, HeapCapsDeleter>;

HeapBuffer makeInternalBuffer(size_t size) {
    return HeapBuffer(static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)));
}

void onAppRenamedHook(const String &appNum, const String &newName) {
    if (!appNum.isEmpty() && !newName.isEmpty()) {
        updateInstalledAppName(appNum, newName);
    }
}
} // namespace

void launcherBackupInit() {
    launcherUiSetAppRenamedCb(onAppRenamedHook);
}

String generateAppNum(const String &sdFilepath) {
    return launcherGenerateAppNum(sdFilepath);
}

static bool readBackupDoc(JsonDocument &doc) {
    if (!setupSdCard()) return false;
    File f = SD.open(BACKUP_DATA_PATH, FILE_READ);
    if (!f) return true; // file not yet created — doc stays empty/null, that's OK
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return err == DeserializationError::Ok || err == DeserializationError::EmptyInput;
}

static bool writeBackupDoc(const JsonDocument &doc) {
    if (!setupSdCard()) return false;
    SD.mkdir("/bkp");
    File f = SD.open(BACKUP_DATA_PATH, FILE_WRITE);
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

static JsonObject findInstalledEntry(JsonDocument &doc, const String &appNum) {
    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return JsonObject();
    for (JsonObject entry : arr) {
        if (entry["appNum"].as<String>() == appNum) return entry;
    }
    return JsonObject();
}

static JsonArray ensureInstalledArray(JsonDocument &doc) {
    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) arr = doc["Installed"].to<JsonArray>();
    return arr;
}

bool saveInstalledToConfig(const BackupInstallInfo &info) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, info.appNum);
    if (entry.isNull()) {
        JsonArray arr = ensureInstalledArray(doc);
        if (arr.isNull()) return false;
        entry = arr.add<JsonObject>();
        if (entry.isNull()) return false;
    }

    entry["appNum"] = info.appNum;
    entry["filepath"] = info.sdFilepath;
    entry["appName"] = info.appName;

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (dataArr.isNull()) dataArr = entry["data"].to<JsonArray>();
    if (dataArr.isNull()) return false;

    for (const BackupPartitionInfo &part : info.partitions) {
        bool found = false;
        for (JsonObject dataEntry : dataArr) {
            if (dataEntry["label"].as<String>() == part.label) {
                dataEntry["type"] = part.type;
                dataEntry["filepath"] = part.lastBackupPath;
                found = true;
                break;
            }
        }
        if (!found) {
            JsonObject dataEntry = dataArr.add<JsonObject>();
            if (!dataEntry.isNull()) {
                dataEntry["type"] = part.type;
                dataEntry["label"] = part.label;
                dataEntry["filepath"] = part.lastBackupPath;
            }
        }
    }

    return writeBackupDoc(doc);
}

bool updateInstalledAppName(const String &appNum, const String &newName) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return false;

    entry["appName"] = newName;
    return writeBackupDoc(doc);
}

bool updateInstalledBackupPath(const String &appNum, const String &label, const String &backupPath) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return false;

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (dataArr.isNull()) return false;

    for (JsonObject dataEntry : dataArr) {
        if (dataEntry["label"].as<String>() == label) {
            dataEntry["filepath"] = backupPath;
            return writeBackupDoc(doc);
        }
    }
    return false;
}

bool removeInstalledFromConfig(const String &appNum) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return false;

    for (size_t i = 0; i < arr.size(); i++) {
        JsonObject entry = arr[i].as<JsonObject>();
        if (!entry.isNull() && entry["appNum"].as<String>() == appNum) {
            arr.remove(i);
            return writeBackupDoc(doc);
        }
    }
    return false;
}

BackupInstallInfo loadInstalledFromConfig(const String &appNum) {
    BackupInstallInfo result;
    JsonDocument doc;
    if (!readBackupDoc(doc)) return result;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return result;

    result.appNum = entry["appNum"].as<String>();
    result.sdFilepath = entry["filepath"].as<String>();
    result.appName = entry["appName"].as<String>();

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (!dataArr.isNull()) {
        for (JsonObject dataEntry : dataArr) {
            BackupPartitionInfo part;
            part.type = dataEntry["type"].as<String>();
            part.label = dataEntry["label"].as<String>();
            part.lastBackupPath = dataEntry["filepath"].as<String>();
            result.partitions.push_back(part);
        }
    }
    return result;
}

std::vector<BackupInstallInfo> listAllBackupEntries() {
    std::vector<BackupInstallInfo> list;
    JsonDocument doc;
    if (!readBackupDoc(doc)) return list;

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return list;

    for (JsonObject entry : arr) {
        BackupInstallInfo info;
        info.appNum = entry["appNum"].as<String>();
        info.sdFilepath = entry["filepath"].as<String>();
        info.appName = entry["appName"].as<String>();
        JsonArray dataArr = entry["data"].as<JsonArray>();
        if (!dataArr.isNull()) {
            for (JsonObject dataEntry : dataArr) {
                BackupPartitionInfo part;
                part.type = dataEntry["type"].as<String>();
                part.label = dataEntry["label"].as<String>();
                part.lastBackupPath = dataEntry["filepath"].as<String>();
                info.partitions.push_back(part);
            }
        }
        if (!info.appNum.isEmpty()) list.push_back(info);
    }
    return list;
}

String findAppNumByFilepath(const String &sdFilepath) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return "";

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return "";

    for (JsonObject entry : arr) {
        if (entry["filepath"].as<String>() == sdFilepath) return entry["appNum"].as<String>();
    }
    return "";
}

String findAppNumByPartitionLabel(const String &partitionLabel) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return "";

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return "";

    for (JsonObject entry : arr) {
        JsonArray dataArr = entry["data"].as<JsonArray>();
        if (dataArr.isNull()) continue;
        for (JsonObject dataEntry : dataArr) {
            if (dataEntry["label"].as<String>() == partitionLabel) return entry["appNum"].as<String>();
        }
    }
    return "";
}

static String sanitizeForPath(const String &name) {
    String out;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') out += c;
        else if (c == ' ') out += '_';
    }
    if (out.length() > 24) out = out.substring(0, 24);
    return out;
}

String backupDirForApp(const String &appNum) {
    if (appNum.isEmpty()) return "";
    if (!setupSdCard()) return "/bkp/" + appNum;

    File root = SD.open("/bkp");
    if (root && root.isDirectory()) {
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            String name = String(f.name());
            bool isDir = f.isDirectory();
            f.close();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (isDir && name.startsWith(appNum)) {
                root.close();
                return "/bkp/" + name;
            }
        }
    }
    if (root) root.close();

    String suffix = sanitizeForPath(loadInstalledFromConfig(appNum).appName);
    if (suffix.isEmpty()) return "/bkp/" + appNum;
    return "/bkp/" + appNum + "-" + suffix;
}

static int nextBackupIndexInDir(const String &dir, const char *type, const char *label) {
    int idx = 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) return 0;

    while (true) {
        char name[64];
        snprintf(name, sizeof(name), "%s.%s.%d.bin", type, label, idx);
        String fullPath = dir + "/" + name;
        if (!SD.exists(fullPath)) break;
        idx++;
    }
    root.close();
    return idx;
}

int nextBackupIndex(const String &appNum, const char *type, const char *label) {
    return nextBackupIndexInDir(backupDirForApp(appNum), type, label);
}

static bool resolveDataRegion(const char *label, uint32_t &flashOffset, uint32_t &regionSize) {
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part) {
        flashOffset = part->address;
        regionSize = part->size;
        return true;
    }

    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrentUnchecked(table)) return false;
    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label);
    if (!entry) return false;
    flashOffset = entry->offset;
    regionSize = entry->size;
    return true;
}

static String backupFail(const char *label) {
    launcherUiError("Backup failed: " + String(label ? label : "data"));
    return "";
}

static bool restoreFail(const char *label) {
    launcherUiError("Restore failed: " + String(label ? label : "data"));
    return false;
}

String backupPartition(const String &appNum, const char *partitionLabel, const char *type) {
    if (!setupSdCard()) {
        launcherUiError("SD card not found");
        return "";
    }

    String dir = backupDirForApp(appNum);
    if (dir.isEmpty()) return backupFail(partitionLabel);
    int idx = nextBackupIndexInDir(dir, type, partitionLabel);
    char fname[64];
    snprintf(fname, sizeof(fname), "%s.%s.%d.bin", type, partitionLabel, idx);
    String outPath = dir + "/" + fname;

    SD.mkdir("/bkp");
    SD.mkdir(dir);

    uint32_t flashOffset = 0;
    uint32_t regionSize = 0;
    if (!resolveDataRegion(partitionLabel, flashOffset, regionSize)) return backupFail(partitionLabel);

    File outFile = SD.open(outPath, FILE_WRITE);
    if (!outFile) return backupFail(partitionLabel);

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        outFile.close();
        SD.remove(outPath);
        launcherUiError("Backup failed: out of memory");
        return "";
    }

    size_t written = 0;
    size_t total = regionSize;
    String stepMsg = String("Backing up ") + partitionLabel + "...";
    launcherUiBeginProgress("Backup Data", stepMsg.c_str());
    launcherUiProgressBytes(0, total, stepMsg.c_str());

    while (written < total) {
        size_t chunk = min(kBackupBufferSize, total - written);
        esp_err_t err = esp_flash_read(nullptr, buf.get(), flashOffset + written, chunk);
        if (err != ESP_OK) {
            outFile.close();
            SD.remove(outPath);
            return backupFail(partitionLabel);
        }
        if (outFile.write(buf.get(), chunk) != chunk) {
            outFile.close();
            SD.remove(outPath);
            return backupFail(partitionLabel);
        }
        written += chunk;
        launcherUiProgressBytes(written, total);
    }
    outFile.close();

    updateInstalledBackupPath(appNum, String(partitionLabel), outPath);
    launcherUiFinish(true, "Backup Saved", outPath.c_str());
    return outPath;
}

bool backupAllPartitionsForApp(const String &appNum) {
    BackupInstallInfo info = loadInstalledFromConfig(appNum);
    if (info.appNum.isEmpty()) return false;

    bool ok = true;
    for (const BackupPartitionInfo &part : info.partitions) {
        String path = backupPartition(appNum, part.label.c_str(), part.type.c_str());
        if (path.isEmpty()) ok = false;
    }
    return ok;
}

bool hasRestorableBackup(const BackupInstallInfo &info) {
    if (info.partitions.empty()) return false;
    if (!setupSdCard()) return false;
    for (const BackupPartitionInfo &part : info.partitions) {
        if (!part.lastBackupPath.isEmpty() && SD.exists(part.lastBackupPath)) return true;
    }
    return false;
}

bool restoreLastBackupForApp(const String &appNum) {
    BackupInstallInfo info = loadInstalledFromConfig(appNum);
    if (!hasRestorableBackup(info)) return false;

    bool ok = true;
    for (const BackupPartitionInfo &part : info.partitions) {
        if (part.lastBackupPath.isEmpty() || !SD.exists(part.lastBackupPath)) continue;
        if (!restorePartitionFromBackup(part.label.c_str(), part.lastBackupPath.c_str())) ok = false;
    }
    return ok;
}

bool restorePartitionFromBackup(const char *partitionLabel, const char *backupFilePath) {
    if (!setupSdCard()) {
        launcherUiError("SD card not found");
        return false;
    }

    File inFile = SD.open(backupFilePath, FILE_READ);
    if (!inFile) return restoreFail(partitionLabel);

    size_t fileSize = inFile.size();
    if (fileSize == 0) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
    if (!part) {
        inFile.close();
        uint32_t flashOffset = 0;
        uint32_t regionSize = 0;
        if (resolveDataRegion(partitionLabel, flashOffset, regionSize) && regionSize > 0) {
            launcherUiBeginProgress("Restore Data", (String("Restoring ") + partitionLabel + "...").c_str());
            bool ok = restorePartitionFromBackupDirect(partitionLabel, backupFilePath, flashOffset, regionSize);
            if (ok) launcherUiFinish(true, "Data Restored", partitionLabel);
            return ok;
        }
        launcherConsolePrintf("restorePartitionFromBackup: partition '%s' not found, skipping\n", partitionLabel);
        return true;
    }

    const bool truncated = fileSize > part->size;
    if (truncated) fileSize = part->size;

    String stepMsg = String("Restoring ") + partitionLabel + "...";
    launcherUiStep(stepMsg.c_str(), -1, "Erasing data partition...");

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        inFile.close();
        launcherUiError("Restore failed: out of memory");
        return false;
    }

    size_t written = 0;
    launcherUiProgressBytes(0, fileSize, stepMsg.c_str());
    while (written < fileSize) {
        size_t chunk = min(kBackupBufferSize, fileSize - written);
        if (inFile.read(buf.get(), chunk) != static_cast<int>(chunk)) {
            inFile.close();
            return restoreFail(partitionLabel);
        }
        err = esp_partition_write(part, written, buf.get(), chunk);
        if (err != ESP_OK) {
            inFile.close();
            return restoreFail(partitionLabel);
        }
        written += chunk;
        launcherUiProgressBytes(written, fileSize);
    }
    inFile.close();

    if (truncated) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(part->address, part->size, &patchError)) {
            launcherConsolePrintf("Restore patch failed label=%s: %s\n", partitionLabel, patchError.c_str());
        }
        launcherUiFinish(true, "Data Restored", "Backup was larger than partition; some data may be truncated.");
    } else {
        launcherUiFinish(true, "Data Restored", partitionLabel);
    }
    return true;
}

bool restorePartitionFromBackupDirect(
    const char *partitionLabel, const char *backupFilePath, uint32_t flashOffset, uint32_t flashSize
) {
    if (!setupSdCard()) {
        launcherUiError("SD card not found");
        return false;
    }

    File inFile = SD.open(backupFilePath, FILE_READ);
    if (!inFile) return restoreFail(partitionLabel);

    size_t fileSize = inFile.size();
    if (fileSize == 0) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    const bool truncated = fileSize > flashSize;
    if (truncated) fileSize = flashSize;

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        inFile.close();
        launcherUiError("Restore failed: out of memory");
        return false;
    }

    size_t written = 0;
    String stepMsg = String("Restoring ") + partitionLabel + "...";
    launcherUiProgressBytes(0, fileSize, stepMsg.c_str());

    while (written < fileSize) {
        size_t chunk = min(kBackupBufferSize, fileSize - written);
        uint32_t addr = flashOffset + written;

        if ((written % kBackupBufferSize) == 0) {
            esp_err_t err = esp_flash_erase_region(nullptr, addr, kBackupBufferSize);
            if (err != ESP_OK) {
                inFile.close();
                return restoreFail(partitionLabel);
            }
        }

        if (inFile.read(buf.get(), chunk) != static_cast<int>(chunk)) {
            inFile.close();
            return restoreFail(partitionLabel);
        }

        esp_err_t err = esp_flash_write(nullptr, buf.get(), addr, chunk);
        if (err != ESP_OK) {
            inFile.close();
            return restoreFail(partitionLabel);
        }

        written += chunk;
        launcherUiProgressBytes(written, fileSize);
    }
    inFile.close();

    if (truncated) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(flashOffset, flashSize, &patchError)) {
            launcherConsolePrintf("Restore patch failed label=%s: %s\n", partitionLabel, patchError.c_str());
        }
    }
    return true;
}
