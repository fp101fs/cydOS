#pragma once

#include "partition_table_model.h"
#include <Arduino.h>

// High-level Storage & Partitions (PMan) backend operations built on top of
// Launcher's partition_table_model, idf_update, and sd_functions.

// Returns true when dynamic flash space is fragmented (e.g. >1 free gap in the
// dynamic pool, or compacting the table would move at least one partition).
bool launcherCanCompactPartitions(const LauncherPartitionTable &table, uint32_t *outLargestFree = nullptr);

// Compacts movable OTA app and data partitions toward the start of the dynamic
// flash pool, migrates any moved flash data, writes the updated partition table,
// prunes NVS app metadata, and notifies the Home Screen.
bool launcherCompactFlashPartitions();

// Dumps the raw 4 KB partition table (0x8000..0x9000) to /dumps/partitions.bin on SD.
bool launcherDumpPartitionTableToSd(String *outPath = nullptr);

// Dumps any flash partition's raw bytes to /dumps/<label>.bin on SD with narrated progress.
bool launcherDumpPartitionToSd(const LauncherPartitionEntry &entry, String *outPath = nullptr);

// Restores a raw .bin file from SD into a writable (non-factory/non-nvs/non-otadata)
// flash partition with narrated progress.
bool launcherRestorePartitionFromSd(const LauncherPartitionEntry &entry, const char *sdPath);

// Removes a removable data partition (FAT/SPIFFS/LittleFS) from the partition table,
// writes the updated table, prunes NVS app metadata, and notifies the Home Screen.
bool launcherDeleteDataPartition(const LauncherPartitionEntry &entry);
