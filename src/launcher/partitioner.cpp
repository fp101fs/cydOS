#include "partitioner.h"
#include "idf_update.h"
#include "launcher_app_registry.h"
#include "launcher_platform.h"
#include "launcher_ui_bridge.h"
#include "littlefs_patch.h"
#include "sd_functions.h"
#include <SD.h>
#include <cctype>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <memory>

namespace {

constexpr size_t kChunkSize = 4096;

struct HeapCapsDeleter {
    void operator()(uint8_t *ptr) const {
        if (ptr) heap_caps_free(ptr);
    }
};
using HeapBuffer = std::unique_ptr<uint8_t, HeapCapsDeleter>;

HeapBuffer allocInternalBuf(size_t sz) {
    return HeapBuffer(static_cast<uint8_t *>(heap_caps_malloc(sz, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)));
}

String sanitizeDumpLabel(const char *label) {
    String out;
    if (!label) return "partition";
    for (size_t i = 0; label[i] != '\0'; ++i) {
        char c = label[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            out += c;
        } else if (c == ' ' || c == '.') {
            out += '_';
        }
    }
    if (out.isEmpty()) out = "partition";
    return out;
}

uint32_t dynamicPoolStart(const LauncherPartitionTable &table) {
    uint32_t start = 0x210000u;
    for (const LauncherPartitionEntry &e : table.entries) {
        if (e.isFactoryOrTestApp()) {
            uint32_t end = e.offset + e.size;
            if (end > start) start = end;
        }
    }
    return start;
}

bool isProtectedSystemPartition(const LauncherPartitionEntry &entry) {
    if (entry.isFactoryOrTestApp()) return true;
    if (entry.isData() && (entry.subtype == 0x02 || entry.subtype == 0x00)) {
        // NVS (0x02) and otadata (0x00)
        return true;
    }
    return false;
}

} // namespace

bool launcherCanCompactPartitions(const LauncherPartitionTable &table, uint32_t *outLargestFree) {
    uint32_t dynStart = dynamicPoolStart(table);
    std::vector<LauncherPartitionRange> freeRanges = launcherPartitionFreeRanges(table);
    uint32_t largest = 0;
    size_t dynFreeCount = 0;
    for (const LauncherPartitionRange &r : freeRanges) {
        if (r.offset + r.size <= dynStart) continue;
        uint32_t s = (r.offset < dynStart) ? (r.offset + r.size - dynStart) : r.size;
        if (s > largest) largest = s;
        if (s > 0) dynFreeCount++;
    }
    if (outLargestFree) *outLargestFree = largest;

    LauncherPartitionTable compacted = table;
    if (!launcherPartitionCompact(compacted)) {
        return dynFreeCount > 1;
    }
    if (compacted.entries.size() != table.entries.size()) return true;
    for (size_t i = 0; i < table.entries.size(); ++i) {
        if (compacted.entries[i].offset != table.entries[i].offset) {
            return true;
        }
    }
    return false;
}

bool launcherCompactFlashPartitions() {
    LauncherPartitionTable current;
    String error;
    if (!launcherPartitionReadCurrent(current, &error)) {
        launcherUiError("Could not read partition table: " + error, "Storage Error");
        return false;
    }

    LauncherPartitionTable target = current;
    if (!launcherPartitionCompact(target, &error)) {
        launcherUiError("Could not plan compaction: " + error, "Compact Failed");
        return false;
    }

    bool anyMoved = false;
    for (size_t i = 0; i < current.entries.size() && i < target.entries.size(); ++i) {
        if (current.entries[i].offset != target.entries[i].offset) {
            anyMoved = true;
            break;
        }
    }
    if (!anyMoved) {
        launcherUiInfo("Flash storage is already compact. No partitions need to be moved.", "Compact Flash");
        return true;
    }

    if (!launcherUiConfirm(
            "Compact Flash?",
            "Moves installed Apps and data closer together to reclaim fragmented free space.\nDo not power off during compaction.",
            "Compact"
        )) {
        return false;
    }

    launcherUiBeginProgress("Compact Flash", "Moving partitions in flash...", 0, false);
    if (!launcherPartitionMigrateMovedData(current, target, &error)) {
        launcherUiError("Failed to move flash data: " + error, "Compact Failed");
        return false;
    }

    launcherUiLockProgress();
    launcherUiStep("Saving partition table...", 90, "");
    if (!launcherPartitionWriteGeneratedTable(target, &error)) {
        launcherUiError("Failed to write partition table: " + error, "Compact Failed");
        return false;
    }

    launcherUiStep("Refreshing app registry...", 100, "");
    launcherPruneAppRegistry(target);
    launcherUiNotifyAppsChanged(nullptr);

    uint32_t largestFree = 0;
    launcherCanCompactPartitions(target, &largestFree);
    char szBuf[24];
    launcherUiFormatBytes(largestFree, szBuf, sizeof(szBuf));
    String detail = String("Largest contiguous free block:\n") + szBuf;
    launcherUiFinish(true, "Flash Compacted", detail.c_str());
    return true;
}

bool launcherDumpPartitionTableToSd(String *outPath) {
    if (!setupSdCard()) {
        launcherUiError("Insert a FAT32 SD card to dump the partition table.", "SD Card Required");
        return false;
    }

    SD.mkdir("/dumps");
    String dest = "/dumps/partitions.bin";

    HeapBuffer buf = allocInternalBuf(LAUNCHER_PARTITION_TABLE_SIZE);
    if (!buf) {
        launcherUiError("Out of memory while dumping partition table.");
        return false;
    }

    launcherUiBeginProgress("Dump Table", "Reading partition table...", 0, false);
    esp_err_t err = esp_flash_read(nullptr, buf.get(), LAUNCHER_PARTITION_TABLE_OFFSET, LAUNCHER_PARTITION_TABLE_SIZE);
    if (err != ESP_OK) {
        launcherUiError("Failed to read partition table from flash.");
        return false;
    }

    File f = SD.open(dest, FILE_WRITE);
    if (!f) {
        launcherUiError("Could not create /dumps/partitions.bin on SD.");
        return false;
    }
    size_t written = f.write(buf.get(), LAUNCHER_PARTITION_TABLE_SIZE);
    f.close();
    if (written != LAUNCHER_PARTITION_TABLE_SIZE) {
        SD.remove(dest);
        launcherUiError("Failed to write /dumps/partitions.bin to SD.");
        return false;
    }

    if (outPath) *outPath = dest;
    launcherUiFinish(true, "Table Dumped", dest.c_str());
    return true;
}

bool launcherDumpPartitionToSd(const LauncherPartitionEntry &entry, String *outPath) {
    if (entry.size == 0) {
        launcherUiError("Selected partition has zero size.");
        return false;
    }
    if (!setupSdCard()) {
        launcherUiError("Insert a FAT32 SD card to dump partitions.", "SD Card Required");
        return false;
    }

    SD.mkdir("/dumps");
    String dest = "/dumps/" + sanitizeDumpLabel(entry.label) + ".bin";

    HeapBuffer buf = allocInternalBuf(kChunkSize);
    if (!buf) {
        launcherUiError("Out of memory while dumping partition.");
        return false;
    }

    File outFile = SD.open(dest, FILE_WRITE);
    if (!outFile) {
        launcherUiError("Could not create dump file on SD:\n" + dest);
        return false;
    }

    String stepMsg = String("Dumping ") + entry.label + " to SD...";
    launcherUiBeginProgress("Dump Partition", stepMsg.c_str(), 0, true);
    launcherUiProgressBytes(0, entry.size, stepMsg.c_str());

    size_t copied = 0;
    while (copied < entry.size) {
        if (launcherUiIsCancelled()) {
            outFile.close();
            SD.remove(dest);
            launcherUiError("Partition dump cancelled.", "Cancelled");
            return false;
        }
        size_t chunk = (entry.size - copied > kChunkSize) ? kChunkSize : (entry.size - copied);
        esp_err_t err = esp_flash_read(nullptr, buf.get(), entry.offset + copied, chunk);
        if (err != ESP_OK) {
            outFile.close();
            SD.remove(dest);
            launcherUiError("Flash read failed during dump.");
            return false;
        }
        if (outFile.write(buf.get(), chunk) != chunk) {
            outFile.close();
            SD.remove(dest);
            launcherUiError("SD write failed during dump.");
            return false;
        }
        copied += chunk;
        launcherUiProgressBytes(copied, entry.size);
    }
    outFile.close();

    if (outPath) *outPath = dest;
    char szBuf[24];
    launcherUiFormatBytes(entry.size, szBuf, sizeof(szBuf));
    String detail = dest + "\n(" + szBuf + ")";
    launcherUiFinish(true, "Dump Saved", detail.c_str());
    return true;
}

bool launcherRestorePartitionFromSd(const LauncherPartitionEntry &entry, const char *sdPath) {
    if (!sdPath || !sdPath[0]) return false;
    if (isProtectedSystemPartition(entry)) {
        launcherUiError("System partitions (factory, nvs, otadata) are protected and cannot be overwritten.", "Protected");
        return false;
    }
    if (!setupSdCard()) {
        launcherUiError("Insert a FAT32 SD card to restore partitions.", "SD Card Required");
        return false;
    }

    File inFile = SD.open(sdPath, FILE_READ);
    if (!inFile) {
        launcherUiError(String("Could not open SD file:\n") + sdPath);
        return false;
    }
    size_t fileSize = inFile.size();
    if (fileSize == 0) {
        inFile.close();
        launcherUiError("Selected .bin file is empty.");
        return false;
    }
    if (fileSize > entry.size) {
        inFile.close();
        char fSz[24], pSz[24];
        launcherUiFormatBytes(fileSize, fSz, sizeof(fSz));
        launcherUiFormatBytes(entry.size, pSz, sizeof(pSz));
        launcherUiError(String("File (") + fSz + ") is larger than partition '" + entry.label + "' (" + pSz + ").");
        return false;
    }

    String msg = String("Overwrite partition '") + entry.label + "' with:\n" + sdPath + "?";
    if (!launcherUiConfirm("Restore Partition?", msg.c_str(), "Restore")) {
        inFile.close();
        return false;
    }

    String stepMsg = String("Restoring ") + entry.label + "...";
    launcherUiBeginProgress("Restore Partition", stepMsg.c_str(), 0, false);
    launcherUiLockProgress();
    launcherUiProgressBytes(0, fileSize, stepMsg.c_str());

    bool isApp = entry.isApp();
    static auto progressBridge = [](size_t written, size_t total) {
        launcherUiProgressBytes(written, total);
    };

    bool ok = launcherRawUpdateStream(inFile, entry.offset, entry.size, fileSize, isApp, progressBridge);
    inFile.close();

    if (!ok) {
        launcherUiError(String("Flash restore failed: ") + launcherUpdateLastErrorName());
        return false;
    }

    if (entry.isData() && entry.subtype == 0x83) {
        String patchErr;
        launcherPatchReducedLittlefsSuperblocks(entry.offset, entry.size, &patchErr);
    }

    launcherUiFinish(true, "Partition Restored", entry.label);
    return true;
}

bool launcherDeleteDataPartition(const LauncherPartitionEntry &entry) {
    if (!launcherPartitionIsRemovableInstallData(entry)) {
        launcherUiError("This partition is protected and cannot be deleted.", "Protected");
        return false;
    }

    char szBuf[24];
    launcherUiFormatBytes(entry.size, szBuf, sizeof(szBuf));
    String msg = String("Delete data partition '") + entry.label + "' (" + szBuf + ")?\nAll data inside it will be lost.";
    if (!launcherUiConfirm("Delete Partition?", msg.c_str(), "Delete", 0xF28B82)) {
        return false;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherUiError("Could not read partition table: " + error);
        return false;
    }

    if (!launcherPartitionRemoveEntryByOffset(table, entry.offset)) {
        launcherUiError("Partition entry no longer found.");
        return false;
    }

    launcherUiBeginProgress("Delete Partition", "Updating partition table...", 0, false);
    launcherUiLockProgress();
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        launcherUiError("Failed to write partition table: " + error);
        return false;
    }

    launcherPruneAppRegistry(table);
    launcherUiNotifyAppsChanged(nullptr);
    launcherUiFinish(true, "Partition Deleted", entry.label);
    return true;
}
