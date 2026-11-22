#include "onlineLauncher.h"
#include "idf_http_client.h"
#include "idf_update.h"
#include "install_shared.h"
#include "launcher_app_registry.h"
#include "launcher_platform.h"
#include "launcher_ui_bridge.h"
#include "littlefs_patch.h"
#include "nvs_helpers.h"
#include "partition_table_model.h"
#include "sd_functions.h"
#include "../drivers/wifi_mgr.h"
#include <SD.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_partition.h>

#define M5_SERVER_PATH "https://m5burner-cdn.m5stack.com/firmware/"

namespace {
constexpr const char *kCfgNs = "l_cfg";
constexpr const char *kDefaultOtaTag = "CYD";
constexpr size_t kExtRangeBufSize = 416;

class LauncherJsonAllocator : public ArduinoJson::Allocator {
public:
    void *allocate(size_t size) override {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }

    void deallocate(void *ptr) override { heap_caps_free(ptr); }

    void *reallocate(void *ptr, size_t newSize) override {
        void *p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }
};

String wifiMacString() {
    String mac = WiFi.macAddress();
    if (mac.isEmpty()) return "00:00:00:00:00:00";
    return mac;
}

String jsonOptString(JsonVariantConst v) {
    return v.is<const char *>() ? String(v.as<const char *>()) : String();
}

uint32_t launcherBootloaderFlashOffset() {
#ifdef CONFIG_BOOTLOADER_OFFSET_IN_FLASH
    return CONFIG_BOOTLOADER_OFFSET_IN_FLASH;
#elif CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    return 0x1000;
#elif CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32C5
    return 0x2000;
#else
    return 0x0;
#endif
}

struct RangeBufferContext {
    uint8_t *buffer;
    size_t capacity;
    size_t written;
};

bool rangeBufferCb(const uint8_t *data, size_t len, void *ctx) {
    RangeBufferContext *range = static_cast<RangeBufferContext *>(ctx);
    if (!range || !range->buffer || range->written + len > range->capacity) return false;
    memcpy(range->buffer + range->written, data, len);
    range->written += len;
    return true;
}

bool discardHttpCb(const uint8_t *, size_t, void *) { return true; }

bool getRemoteFileSize(const String &url, size_t &size, const char *hwid = nullptr) {
    String activeUrl = url;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        LauncherHttpResponse response;
        bool ok = launcherHttpGetRange(activeUrl.c_str(), 0, 1, discardHttpCb, nullptr, &response, hwid);
        if (ok && response.status == 206) {
            return launcherParseContentRangeTotal(response.content_range, size);
        }
        const bool shouldTryProxy =
            activeUrl.indexOf("/download?") >= 0 &&
            (response.status == 0 || (response.status >= 300 && response.status < 400));
        if (attempt == 0 && shouldTryProxy) {
            activeUrl.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for size probe\n");
            continue;
        }
        return false;
    }
    return false;
}

struct FileDownloadContext {
    File *file;
    size_t downloaded;
    size_t expected;
    long progressTick;
    LauncherHttpResponse *response;
    const char *stepText;
};

bool fileDownloadCb(const uint8_t *data, size_t len, void *ctx) {
    FileDownloadContext *download = static_cast<FileDownloadContext *>(ctx);
    if (!download || !download->file || launcherUiIsCancelled()) return false;

    if (download->expected == 0 && download->response && download->response->content_length > 0) {
        download->expected = static_cast<size_t>(download->response->content_length);
        launcherUiProgressBytes(0, download->expected, download->stepText);
    }

    size_t totalWrote = 0;
    constexpr size_t sdWriteChunk = 512;
    while (totalWrote < len) {
        const size_t part = min(sdWriteChunk, len - totalWrote);
        size_t wrote = download->file->write(data + totalWrote, part);
        if (wrote != part) {
            download->file->flush();
            launcherDelayMs(20);
            wrote = download->file->write(data + totalWrote, part);
        }
        if (wrote != part) {
            launcherConsolePrintf(
                "Download: SD write failed at pos 0x%06X (%u/%u bytes)\n",
                static_cast<unsigned>(download->downloaded + totalWrote),
                static_cast<unsigned>(wrote),
                static_cast<unsigned>(part)
            );
            return false;
        }
        totalWrote += wrote;
    }
    download->downloaded += totalWrote;

    if (download->expected > 0) {
        if (download->progressTick >= 8) {
            download->file->flush();
            launcherUiProgressBytes(download->downloaded, download->expected);
            download->progressTick = 0;
        } else {
            download->progressTick++;
        }
    } else if (download->progressTick >= 8) {
        download->file->flush();
        launcherUiProgressBytes(download->downloaded, 0);
        download->progressTick = 0;
    } else {
        download->progressTick++;
    }
    return true;
}

struct RawHttpUpdateContext {
    uint32_t address;
    size_t partitionSize;
    size_t skipRemaining;
    size_t expected;
    size_t written;
    bool appImage;
    bool started;
    const char *stepText;
};

bool launcherRawUpdateHttpCb(const uint8_t *data, size_t len, void *ctx) {
    RawHttpUpdateContext *updateCtx = static_cast<RawHttpUpdateContext *>(ctx);
    if (!updateCtx || launcherUiIsCancelled()) return false;
    if (updateCtx->skipRemaining > 0) {
        const size_t skip = min(len, updateCtx->skipRemaining);
        data += skip;
        len -= skip;
        updateCtx->skipRemaining -= skip;
        if (len == 0) return true;
    }
    if (!updateCtx->started) {
        if (!launcherRawUpdateBegin(
                updateCtx->address, updateCtx->partitionSize, updateCtx->expected, updateCtx->appImage
            )) {
            return false;
        }
        updateCtx->started = true;
        launcherUiProgressBytes(0, updateCtx->expected, updateCtx->stepText);
    }
    const size_t remaining =
        updateCtx->written < updateCtx->expected ? updateCtx->expected - updateCtx->written : 0;
    const size_t writeLen = len > remaining ? remaining : len;
    if (writeLen == 0) return true;
    size_t wrote = launcherRawUpdateWrite(data, writeLen);
    if (wrote != writeLen) return false;
    updateCtx->written += wrote;
    launcherUiProgressBytes(updateCtx->written, updateCtx->expected);
    return true;
}

bool flashRawRangeFromHttp(
    const String &url, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target,
    bool appImage, const char *stepText, const char *hwid = nullptr, String *errorOut = nullptr
) {
    String activeUrl = url;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        RawHttpUpdateContext update = {
            target.offset, target.size, sourceOffset, imageSize, 0, appImage, false, stepText
        };
        LauncherHttpResponse response;
        bool httpOk =
            launcherHttpGetStream(activeUrl.c_str(), launcherRawUpdateHttpCb, &update, &response, "HWID", hwid);

        if (attempt == 0 && update.written == 0 && !launcherUiIsCancelled() &&
            activeUrl.indexOf("/download?") >= 0 &&
            (response.status == 0 || (response.status >= 300 && response.status < 400))) {
            activeUrl.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for OTA flash\n");
            launcherUiStep("Retrying via mirror...", -1, "");
            continue;
        }

        bool complete = update.written == imageSize;
        bool endOk = complete && launcherRawUpdateEnd();
        bool ok = complete && endOk;
        if (ok && !appImage) {
            String patchError;
            if (!launcherPatchReducedLittlefsSuperblocks(target, &patchError)) {
                launcherConsolePrintf(
                    "LittleFS patch failed after HTTP copy label=%s offset=0x%08X size=0x%08X: %s\n",
                    target.label,
                    target.offset,
                    target.size,
                    patchError.c_str()
                );
                ok = false;
            }
        }
        if (!ok && errorOut) {
            if (launcherUiIsCancelled()) {
                *errorOut = "Cancelled";
            } else if (launcherUpdateLastError() != LAUNCHER_UPDATE_ERROR_OK) {
                *errorOut = launcherUpdateLastErrorName();
            } else if (!httpOk && response.transport_error != 0) {
                *errorOut = String("Network error ") + response.transport_error;
            } else if (!httpOk && response.status != 0) {
                *errorOut = response.status >= 300 && response.status < 400
                                ? String("Redirect ") + response.status + " not followed"
                                : String("HTTP status ") + response.status;
            } else if (!complete) {
                *errorOut = String("Download incomplete (") + update.written + "/" + imageSize + ")";
            } else {
                *errorOut = "Unknown failure";
            }
        }
        return ok;
    }
    return false;
}

bool getInfo(
    const String &serverUrl, JsonDocument &outDoc, bool useListFilter = false, int *outStatus = nullptr,
    String *errorOut = nullptr
) {
    if (!launcherIsWifiConnected()) {
        if (errorOut) *errorOut = "Wi-Fi not connected";
        return false;
    }

    constexpr uint8_t maxAttempts = 3;
    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
        String payload;
        LauncherHttpResponse resp;
        launcherConsolePrintf("getInfo: GET attempt %u url_len=%u\n", attempt + 1, serverUrl.length());
        if (launcherHttpGetToString(serverUrl.c_str(), payload, 65536, &resp)) {
            launcherConsolePrintf("getInfo: GET ok status=%d bytes=%u\n", resp.status, payload.length());
            outDoc.clear();
            DeserializationError error;
            if (useListFilter) {
                JsonDocument filter;
                buildFirmwareListFilter(filter);
                error = deserializeJson(outDoc, payload, DeserializationOption::Filter(filter));
            } else {
                error = deserializeJson(outDoc, payload);
            }
            if (error) {
                if (errorOut) *errorOut = String("JSON parse failed: ") + error.c_str();
                outDoc.clear();
                return false;
            }
            return true;
        }

        String reason;
        if (resp.status >= 200 && resp.status < 600 && resp.status != 0) {
            reason = String("HTTP ") + resp.status;
        } else if (resp.error_detail[0]) {
            reason = String("Net ") + resp.transport_error + ": " + resp.error_detail;
        } else {
            reason = String("Network error ") + resp.transport_error;
        }
        launcherConsolePrintf(
            "getInfo: GET failed attempt=%u status=%d transport=%d\n",
            attempt + 1,
            resp.status,
            resp.transport_error
        );
        if (outStatus) *outStatus = resp.status;
        if (errorOut) *errorOut = reason;

        if (resp.status >= 400 && resp.status < 500) {
            return false;
        }
        if (!launcherIsWifiConnected()) {
            if (errorOut) *errorOut = "Wi-Fi lost during fetch";
            return false;
        }
        launcherDelayMs(400);
    }

    if (errorOut && errorOut->isEmpty()) *errorOut = "Server unreachable";
    return false;
}

String resolveDataPartitionSource(JsonObject part, JsonObject sources) {
    String src = jsonOptString(part["source"]);
    if (src.isEmpty() || src == "firmware" || sources.isNull()) return String();
    String url = jsonOptString(sources[src]);
    if (url.isEmpty()) return String();
    return launcherBuildSourceUrl(String(), url, false);
}

String s_lastInstalledLabel;
String s_lastDownloadedPath;

void launchLastInstalledAppCb() {
    if (!s_lastInstalledLabel.isEmpty()) {
        launcherBootAppByLabel(s_lastInstalledLabel.c_str());
    }
}

void installLastDownloadedSdCb() {
    if (!s_lastDownloadedPath.isEmpty()) {
        updateFromSD(s_lastDownloadedPath, true, false);
    }
}

bool installFirmwareDynamic(
    const String &fileAddr, const String &file, uint32_t appSize, uint32_t appPartitionSize,
    uint32_t appOffset, bool nb, std::vector<LauncherInstallDataPartition> &dataPartitions,
    const String &installedName, bool rebootAfter
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherUiIsProgressOpen()) {
        launcherUiBeginProgress("Installing App", "Checking storage layout...", 0, true);
    } else {
        launcherUiStep("Checking storage layout...", -1, "");
    }
    if (launcherUiIsCancelled()) {
        launcherUiError("Install cancelled.", "Cancelled");
        return false;
    }
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherUiError(error.length() ? error : "Could not read partition table");
        return false;
    }

    size_t updateSize = appSize;
    String hwid = wifiMacString();
    if (updateSize == 0) {
        launcherUiStep("Checking download size...", -1, "");
        size_t remoteSize = 0;
        if (!getRemoteFileSize(fileAddr, remoteSize, hwid.c_str())) {
            launcherUiError(launcherUiIsCancelled() ? String("Install cancelled.")
                                                    : String("Could not determine app size"),
                            launcherUiIsCancelled() ? "Cancelled" : "Error");
            return false;
        }
        if (launcherUiIsCancelled()) {
            launcherUiError("Install cancelled.", "Cancelled");
            return false;
        }
        if (nb) {
            updateSize = remoteSize;
        } else {
            if (appOffset >= remoteSize) {
                launcherUiError("Invalid app offset");
                return false;
            }
            updateSize = remoteSize - appOffset;
        }
    }
    if (updateSize == 0) {
        launcherUiError("Invalid app size");
        return false;
    }
    if (appPartitionSize == 0 || appPartitionSize < updateSize) appPartitionSize = updateSize;

    String appLabel = launcherInstallNextAppLabel(table, file, installedName);
    LauncherPartitionEntry appEntry;

    launcherUiStep("Partitioning flash storage...", -1, "");
    if (!launcherSelectInstallLayout(table, appPartitionSize, appLabel, dataPartitions, appEntry, error)) {
        if (error == "Canceled") {
            launcherUiError("Install cancelled.", "Cancelled");
        } else {
            launcherUiError(error.length() ? error : "Not enough flash space");
        }
        return false;
    }

    // Reopen progress modal cleanly if launcherSelectInstallLayout showed a space-picker prompt.
    if (!launcherUiIsProgressOpen()) {
        launcherUiBeginProgress("Installing App", "Downloading & flashing app...", 0, true);
    } else {
        launcherUiStep("Downloading & flashing app...", 0, "Connecting...");
    }

    {
        String appError;
        if (!flashRawRangeFromHttp(
                fileAddr, nb ? 0 : appOffset, updateSize, appEntry, true,
                "Downloading & flashing app...", hwid.c_str(), &appError
            )) {
            if (launcherUiIsCancelled()) {
                launcherUiError("Install cancelled.", "Cancelled");
            } else {
                launcherUiError(String("App install failed: ") + appError);
            }
            return false;
        }
    }

    bool hasEmptyDataPart = false;
    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry) continue;
        if (dp.copySize == 0) {
            hasEmptyDataPart = true;
            continue;
        }
        const char *typeStr = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
        String stepText = String("Setting up ") + typeStr + " data...";
        launcherUiStep(stepText.c_str(), 0, "Connecting...");
        const uint32_t copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        const String &partAddr = dp.sourceUrl.isEmpty() ? fileAddr : dp.sourceUrl;
        String dpError;
        if (!flashRawRangeFromHttp(
                partAddr, dp.sourceOffset, copySize, dp.entry, false, stepText.c_str(), hwid.c_str(),
                &dpError
            )) {
            if (launcherUiIsCancelled()) {
                launcherUiError("Install cancelled.", "Cancelled");
            } else {
                launcherUiError(String(typeStr) + " setup failed: " + dpError);
            }
            return false;
        }
    }
    if (hasEmptyDataPart) {
        launcherUiStep("Preparing app data partition...", -1, "");
    }

    // Lock modal before committing partition table & boot slot to flash.
    launcherUiLockProgress();

    launcherUiStep("Saving storage layout...", -1, "");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        launcherUiError(error.length() ? error : "Failed writing partition table");
        return false;
    }

    launcherUiStep("Configuring boot slot...", -1, "");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        launcherUiError(error.length() ? error : "Failed setting boot slot");
        return false;
    }

    String shownName = installedName.isEmpty() ? launcherInstallAppDisplayName(file) : installedName;
    if (shownName.isEmpty()) shownName = String(appEntry.label);

    launcherUiStep("Adding icon to Home Screen...", 100, shownName.c_str());
    {
        std::vector<String> fatLabels;
        String registeredSpiffsLabel;
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            if (dp.subtype == 0x81) fatLabels.push_back(dp.label);
            else registeredSpiffsLabel = dp.label;
        }
        launcherSaveInstalledAppMetadata(
            table, appEntry, file, installedName, fatLabels, registeredSpiffsLabel
        );
    }

    launcherUiSaveState();

    if (rebootAfter) {
        launcherUiStep("Starting app...", 100, shownName.c_str());
        launcherDelayMs(400);
        return launcherUiReboot();
    }

    s_lastInstalledLabel = String(appEntry.label);
    String detail = launcherFormatAppInstalledDetail(shownName);
    launcherUiFinishWithPrimaryAction(
        true, "App Installed", detail.c_str(), "Launch Now", launchLastInstalledAppCb, "Done"
    );
    return true;
}

struct MergedDownloadContext {
    File *file;
    size_t *pos;
    size_t sourceExpected;
    size_t sourceWritten;
    long progressTick;
    LauncherHttpResponse *response;
    uint8_t *captureBuf;
    size_t captureStart;
    size_t captureLen;
    const char *stepText;
};

bool mergedDownloadCb(const uint8_t *data, size_t len, void *ctx) {
    MergedDownloadContext *d = static_cast<MergedDownloadContext *>(ctx);
    if (!d || !d->file || launcherUiIsCancelled()) return false;

    if (d->sourceExpected == 0 && d->response && d->response->content_length > 0) {
        d->sourceExpected = static_cast<size_t>(d->response->content_length);
        launcherUiProgressBytes(0, d->sourceExpected, d->stepText);
    }

    const size_t absStart = *d->pos;
    size_t totalWrote = 0;
    constexpr size_t sdWriteChunk = 512;
    while (totalWrote < len) {
        const size_t part = min(sdWriteChunk, len - totalWrote);
        size_t written = d->file->write(data + totalWrote, part);
        if (written != part) {
            d->file->flush();
            launcherDelayMs(20);
            written = d->file->write(data + totalWrote, part);
        }
        if (written != part) {
            launcherConsolePrintf(
                "Merge: SD write failed at pos 0x%06X (%u/%u bytes)\n",
                static_cast<unsigned>(*d->pos + totalWrote),
                static_cast<unsigned>(written),
                static_cast<unsigned>(part)
            );
            return false;
        }
        totalWrote += part;
    }
    d->sourceWritten += totalWrote;
    *d->pos += totalWrote;

    if (d->captureBuf && d->captureLen) {
        const size_t capEnd = d->captureStart + d->captureLen;
        const size_t s = max(absStart, d->captureStart);
        const size_t e = min(absStart + len, capEnd);
        if (s < e) memcpy(d->captureBuf + (s - d->captureStart), data + (s - absStart), e - s);
    }

    if (d->sourceExpected > 0) {
        if (d->progressTick >= 8) {
            d->file->flush();
            launcherUiProgressBytes(d->sourceWritten, d->sourceExpected);
            d->progressTick = 0;
        } else {
            d->progressTick++;
        }
    }
    return true;
}

bool padMergedFile(File &file, size_t &pos, size_t target) {
    if (pos > target) {
        launcherConsolePrintf(
            "Merge: overlap while placing source at 0x%06X (pos 0x%06X)\n",
            static_cast<unsigned>(target),
            static_cast<unsigned>(pos)
        );
        return false;
    }
    constexpr size_t padProgressThreshold = 128 * 1024;
    constexpr size_t padProgressStep = 64 * 1024;
    const size_t total = target - pos;
    const bool report = total > padProgressThreshold;
    if (report) {
        launcherUiProgressBytes(0, total, "Aligning image on SD...");
    }

    uint8_t ff[4096];
    memset(ff, 0xFF, sizeof(ff));
    size_t done = 0;
    size_t lastReported = 0;
    while (pos < target) {
        if (launcherUiIsCancelled()) return false;
        const size_t chunk = min(sizeof(ff), target - pos);
        size_t written = file.write(ff, chunk);
        if (written != chunk) {
            file.flush();
            launcherDelayMs(20);
            written = file.write(ff, chunk);
        }
        if (written != chunk) return false;
        pos += chunk;
        done += chunk;
        if (done - lastReported >= padProgressStep) {
            lastReported = done;
            file.flush();
            if (report) launcherUiProgressBytes(done, total);
        }
    }
    if (report) file.flush();
    return true;
}

bool mergeSourceIntoFile(
    File &file, size_t &pos, size_t offset, const String &fid, const String &sourceUrl, bool useProxy,
    const char *stepText, uint8_t *captureBuf = nullptr, size_t captureStart = 0, size_t captureLen = 0
) {
    if (sourceUrl.isEmpty()) return false;
    if (!padMergedFile(file, pos, offset)) return false;
    String activeUrl = launcherBuildSourceUrl(fid, sourceUrl, useProxy);
    String hwid = wifiMacString();

    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        LauncherHttpResponse resp;
        MergedDownloadContext ctx = {
            &file, &pos, 0, 0, 0, &resp, captureBuf, captureStart, captureLen, stepText
        };
        bool ok = launcherHttpGetStream(
            activeUrl.c_str(), mergedDownloadCb, &ctx, &resp, "HWID", hwid.c_str()
        );
        file.flush();

        if (ok && resp.status == 200) {
            if (resp.content_length > 0 && ctx.sourceWritten != static_cast<size_t>(resp.content_length)) {
                return false;
            }
            return true;
        }

        const bool unfollowedRedirect = resp.status >= 300 && resp.status < 400;
        if (attempt == 0 && ctx.sourceWritten == 0 && unfollowedRedirect &&
            activeUrl.indexOf("/download?") >= 0) {
            activeUrl.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for source download\n");
            launcherUiStep("Retrying via mirror...", -1, "");
            continue;
        }
        return false;
    }
    return false;
}

bool downloadSplitFirmware(
    const String &fid, JsonObject install, const String &filePath, const String &folder,
    const String &version
) {
    JsonObject sources = install["sources"].as<JsonObject>();
    String blUrl = jsonOptString(sources["bootloader"]);
    String partUrl = jsonOptString(sources["partitions"]);
    String fwUrl = jsonOptString(sources["firmware"]);
    String dataUrl = jsonOptString(sources["data"]);

    const bool mergedFirmware = blUrl.isEmpty() && partUrl.isEmpty();
    const bool hasData = !dataUrl.isEmpty();

    uint32_t appOffset = install["app"]["flash_offset"] | 0;
    uint32_t dataOffset = 0;
    for (JsonObject part : install["partitions"].as<JsonArray>()) {
        String type = part["type"].as<String>();
        String subtype = part["subtype"].as<String>();
        if (type == "app" && subtype == "ota" && appOffset == 0) {
            appOffset = part["flash_offset"] | 0;
        } else if (type == "data" && dataOffset == 0) {
            dataOffset = part["flash_offset"] | 0;
        }
    }
    if (appOffset == 0) appOffset = 0x10000;

    const uint32_t blOffset = launcherBootloaderFlashOffset();
    const uint32_t partOffset = LAUNCHER_PARTITION_TABLE_OFFSET;

    launcherUiStep("Creating file on SD card...", -1, "");
    File file = SD.open(filePath, FILE_WRITE);
    if (!file) {
        launcherUiError("Could not create file on SD card");
        return false;
    }

    size_t pos = 0;
    bool ok = true;
    if (mergedFirmware) {
        uint8_t ptable[LAUNCHER_PARTITION_TABLE_SIZE];
        memset(ptable, 0xFF, sizeof(ptable));
        launcherUiStep("Downloading app image...", 0, "Connecting...");
        ok = mergeSourceIntoFile(
            file, pos, 0x0, fid, fwUrl, true, "Downloading app image...",
            hasData ? ptable : nullptr, partOffset, sizeof(ptable)
        );
        if (ok && hasData) {
            if (dataOffset == 0) {
                dataOffset = launcherFirstDataPartitionOffsetFromTable(ptable, sizeof(ptable));
            }
            if (dataOffset == 0 || dataOffset < pos) {
                launcherConsolePrintf(
                    "Merge: could not resolve data partition offset (0x%06X)\n",
                    static_cast<unsigned>(dataOffset)
                );
                ok = false;
            } else {
                launcherUiStep("Downloading app data...", 0, "Connecting...");
                ok = mergeSourceIntoFile(file, pos, dataOffset, fid, dataUrl, false, "Downloading app data...");
            }
        }
    } else {
        if (ok && !blUrl.isEmpty()) {
            launcherUiStep("Downloading bootloader...", 0, "Connecting...");
            ok = mergeSourceIntoFile(file, pos, blOffset, fid, blUrl, false, "Downloading bootloader...");
        }
        if (ok && !partUrl.isEmpty()) {
            launcherUiStep("Downloading partition table...", 0, "Connecting...");
            ok = mergeSourceIntoFile(file, pos, partOffset, fid, partUrl, false, "Downloading partition table...");
        }
        if (ok) {
            launcherUiStep("Downloading app image...", 0, "Connecting...");
            ok = mergeSourceIntoFile(file, pos, appOffset, fid, fwUrl, true, "Downloading app image...");
        }
        if (ok && hasData) {
            if (dataOffset == 0) {
                ok = false;
            } else {
                launcherUiStep("Downloading app data...", 0, "Connecting...");
                ok = mergeSourceIntoFile(file, pos, dataOffset, fid, dataUrl, false, "Downloading app data...");
            }
        }
    }
    file.flush();
    file.close();

    if (!ok || launcherUiIsCancelled()) {
        SD.remove(filePath);
        launcherUiError(launcherUiIsCancelled() ? "Download cancelled." : "Download failed.",
                        launcherUiIsCancelled() ? "Cancelled" : "Error");
        return false;
    }

    launcherUiStep("Verifying SD file...", 100, "");
    size_t finalSize = pos;
    File checkFile = SD.open(filePath, FILE_READ);
    if (checkFile) {
        finalSize = checkFile.size();
        checkFile.close();
    }

    saveDownloadedFirmware(folder, fid, version);
    s_lastDownloadedPath = filePath;
    String detail = launcherFormatDownloadSavedDetail(filePath, finalSize);
    launcherUiFinishWithPrimaryAction(
        true, "Saved to SD", detail.c_str(), "Install Now", installLastDownloadedSdCb, "Done"
    );
    return true;
}
} // namespace

ArduinoJson::Allocator *launcherJsonAllocator() {
    static LauncherJsonAllocator instance;
    return &instance;
}

JsonDocument g_launcherHubDoc(launcherJsonAllocator());
int g_launcherTotalFirmware = 0;
int g_launcherNumPages = 0;
uint8_t g_launcherCurrentPage = 1;
String g_launcherOtaTag = kDefaultOtaTag;
String g_launcherDwnPath = "/downloads/";

void launcherOnlineInit() {
    String storedTag = lnvs::getString(kCfgNs, "otaTag", 31);
    storedTag.trim();
    g_launcherOtaTag = storedTag.isEmpty() ? String(kDefaultOtaTag) : storedTag;
}

void launcherSetOtaTag(const String &tag) {
    String clean = tag;
    clean.trim();
    if (clean.isEmpty()) clean = kDefaultOtaTag;
    g_launcherOtaTag = clean;
    lnvs::setString(kCfgNs, "otaTag", g_launcherOtaTag.c_str());
}

const String &launcherGetOtaTag() { return g_launcherOtaTag; }

void buildFirmwareListFilter(JsonDocument &filter) {
    filter["total"] = true;
    filter["page"] = true;
    filter["page_size"] = true;
    JsonObject item = filter["items"].add<JsonObject>();
    item["fid"] = true;
    item["file"] = true;
    item["name"] = true;
    item["version"] = true;
    item["author"] = true;
    item["star"] = true;
}

String launcherEncodeQueryValue(const String &value) {
    String encoded;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }
    return encoded;
}

String launcherSanitizeFilename(String input) {
    const char charsToReplace[] = {'<', '>', ':', '\"', '/', '\\', '|', '?', '*', '\'', '`', '&'};
    const char replacementChar = '_';

    for (size_t i = 0; i < sizeof(charsToReplace); i++) {
        input.replace(String(charsToReplace[i]), String(replacementChar));
    }
    for (size_t i = 0; i < input.length(); i++) {
        if (static_cast<unsigned char>(input[i]) < 32) {
            input.setCharAt(i, replacementChar);
        }
    }
    input.trim();
    while (input.endsWith(".") || input.endsWith(" ")) {
        input.remove(input.length() - 1);
    }
    if (input.isEmpty()) input = "firmware";
    return input;
}

bool launcherParseContentRangeTotal(const char *contentRange, size_t &total) {
    if (!contentRange) return false;
    String range = contentRange;
    int slash = range.lastIndexOf('/');
    if (slash < 0 || slash + 1 >= static_cast<int>(range.length())) return false;
    long parsed = range.substring(slash + 1).toInt();
    if (parsed <= 0) return false;
    total = static_cast<size_t>(parsed);
    return true;
}

String launcherBuildSourceUrl(const String &fid, const String &sourceUrl, bool useProxy) {
    String url = sourceUrl;
    if (!url.startsWith("http")) url = String(M5_SERVER_PATH) + url;
    if (useProxy && !fid.isEmpty()) {
        url = String("https://api.launcherhub.net/download?fid=") + fid + "&file=" + url;
    }
    return url;
}

uint32_t launcherFirstDataPartitionOffsetFromTable(const uint8_t *table, size_t len) {
    if (!table) return 0;
    for (size_t i = 0; i + LAUNCHER_PARTITION_ENTRY_SIZE <= len; i += LAUNCHER_PARTITION_ENTRY_SIZE) {
        const uint8_t *e = table + i;
        if (e[0] != 0xAA || e[1] != 0x50) break;
        if (e[0x02] == 0x01 && e[0x03] > 0x79) {
            return static_cast<uint32_t>(e[0x04]) | (static_cast<uint32_t>(e[0x05]) << 8) |
                   (static_cast<uint32_t>(e[0x06]) << 16) | (static_cast<uint32_t>(e[0x07]) << 24);
        }
    }
    return 0;
}

String launcherFormatAppInstalledDetail(const String &appName) {
    String clean = appName;
    clean.trim();
    if (clean.isEmpty()) clean = "App";
    return clean + " added to Home Screen.\nTap Launch Now to start it.";
}

String launcherFormatDownloadSavedDetail(const String &filePath, size_t fileSizeBytes) {
    int slash = filePath.lastIndexOf('/');
    String name = (slash >= 0 && slash + 1 < static_cast<int>(filePath.length()))
                      ? filePath.substring(slash + 1)
                      : filePath;
    String dir = (slash > 0) ? filePath.substring(0, slash + 1) : String("/");
    if (name.isEmpty()) name = "app.bin";

    String out = name;
    if (fileSizeBytes > 0) {
        char szBuf[20];
        launcherUiFormatBytes(fileSizeBytes, szBuf, sizeof(szBuf));
        out += " (";
        out += szBuf;
        out += ")";
    }
    out += "\nSaved to ";
    out += dir;
    return out;
}

bool launcherIsWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool ensureWifiConnected() {
    if (launcherIsWifiConnected()) return true;
    if (!wifiMgrHasSavedCreds()) {
        launcherUiError("Connect to Wi-Fi in Settings first.", "Wi-Fi Required");
        return false;
    }

    const char *ssid = wifiMgrGetSavedSsid();
    const char *pass = wifiMgrGetSavedPass();
    launcherUiBeginProgress("Connecting Wi-Fi", (String("Joining ") + ssid + "...").c_str(), 0, true);
    wifiMgrConnect(ssid, pass, false);

    uint32_t startMs = launcherMillis();
    while (launcherMillis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
        if (launcherUiIsCancelled()) {
            launcherUiClose();
            return false;
        }
        wifiMgrTick();
        if (launcherIsWifiConnected()) {
            launcherUiClose();
            return true;
        }
        launcherUiStep(nullptr, -1);
        launcherDelayMs(50);
    }

    launcherUiError("Could not connect to Wi-Fi. Check Settings.", "Wi-Fi Timeout");
    return false;
}

bool GetJsonFromLauncherHub(
    uint8_t page, const String &order, bool star, const String &query, String *errorOut
) {
    String q = "&order_by=" + order;
    if (page > 1) q += "&page=" + String(page);
    if (query.length() > 0) q += "&q=" + launcherEncodeQueryValue(query);
    if (star) q += "&star=1";

    String serverUrl = "https://api.launcherhub.net/firmwares?category=" + g_launcherOtaTag + q;

    int status = 0;
    String err;
    if (getInfo(serverUrl, g_launcherHubDoc, true, &status, &err)) {
        g_launcherTotalFirmware = g_launcherHubDoc["total"].as<int>();
        int pageSize = g_launcherHubDoc["page_size"].as<int>();
        if (pageSize <= 0) pageSize = 1;
        g_launcherNumPages = (g_launcherTotalFirmware + pageSize - 1) / pageSize;
        if (g_launcherNumPages < 1) g_launcherNumPages = 1;
        g_launcherCurrentPage = page;
        return true;
    }

    if (errorOut) {
        if (status == 404) *errorOut = "No apps found for device tag '" + g_launcherOtaTag + "'";
        else *errorOut = err.isEmpty() ? String("Could not load catalog") : err;
    }
    return false;
}

JsonDocument getVersionInfo(const String &fid, String *errorOut) {
    JsonDocument versions(launcherJsonAllocator());
    String serverUrl = "https://api.launcherhub.net/firmwares?fid=" + fid;
    getInfo(serverUrl, versions, false, nullptr, errorOut);
    return versions;
}

bool installFirmwareFromManifest(
    const String &fid, const String &version, String installedName, bool rebootAfter
) {
    if (!ensureWifiConnected()) return false;

    launcherUiBeginProgress("Installing App", "Connecting to LauncherHub...", 0, true);
    JsonDocument detail(launcherJsonAllocator());
    String serverUrl =
        "https://api.launcherhub.net/firmwares?fid=" + fid + "&version=" + launcherEncodeQueryValue(version);
    String fetchErr;
    if (!getInfo(serverUrl, detail, false, nullptr, &fetchErr)) {
        if (launcherUiIsCancelled()) {
            launcherUiError("Install cancelled.", "Cancelled");
        } else {
            launcherUiError(fetchErr.isEmpty() ? String("Failed to fetch install manifest") : fetchErr);
        }
        return false;
    }
    if (launcherUiIsCancelled()) {
        launcherUiError("Install cancelled.", "Cancelled");
        return false;
    }

    launcherUiStep("Reading app package...", -1, "");

    JsonObject versionObj = detail["version"].as<JsonObject>();
    JsonObject install = versionObj["install"].as<JsonObject>();
    JsonObject app = install["app"].as<JsonObject>();
    JsonArray partitions = install["partitions"].as<JsonArray>();
    JsonObject sources = install["sources"].as<JsonObject>();
    String file = versionObj["file"].as<String>();
    if (file.isEmpty() || app.isNull()) {
        launcherUiError("Invalid install manifest");
        return false;
    }

    uint32_t appOffset = app["source_offset"] | 0;
    uint32_t appCopySize = app["image_size"] | 0;
    uint32_t appPartitionSize = appCopySize;
    bool nb = appOffset == 0;
    std::vector<LauncherInstallDataPartition> dataPartitions;

    for (JsonObject part : partitions) {
        String type = part["type"].as<String>();
        String subtype = part["subtype"].as<String>();
        if (type == "app" && subtype == "ota") {
            appOffset = part["source_offset"] | appOffset;
            appCopySize = part["copy_size"] | appCopySize;
            appPartitionSize = appCopySize;
            nb = appOffset == 0;
        } else if (type == "data" && (subtype == "spiffs" || subtype == "littlefs")) {
            LauncherInstallDataPartition dp;
            dp.subtype = (subtype == "littlefs") ? 0x83 : 0x82;
            uint32_t declaredSize = part["size"] | 0;
            dp.sourceOffset = part["source_offset"] | 0;
            dp.copySize = part["copy_size"] | 0;
            dp.sourceUrl = resolveDataPartitionSource(part, sources);
            dp.label = part["label"].as<String>();
            if (dp.label.isEmpty()) dp.label = "spiffs";
            if (dp.label != "label" && dp.copySize > 0 && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
                dp.partitionSize = declaredSize;
            } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE;
            } else {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            }
            dataPartitions.push_back(dp);
        } else if (type == "data" && subtype == "fat") {
            LauncherInstallDataPartition dp;
            dp.subtype = 0x81;
            dp.label = part["label"].as<String>();
            uint32_t declaredSize = part["size"] | 0;
            dp.sourceOffset = part["source_offset"] | 0;
            dp.sourceUrl = resolveDataPartitionSource(part, sources);
            uint32_t requestedCopySize = part["copy_size"] | 0;
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, requestedCopySize);
            dp.partitionSize = payload.partitionSize;
            dp.copySize = payload.copySize;
            dataPartitions.push_back(dp);
        }
    }

    if (appCopySize == 0 || appPartitionSize == 0) {
        launcherUiError("Invalid app size in manifest");
        return false;
    }

    String fileAddr = launcherBuildSourceUrl(fid, file, true);
    if (!file.startsWith("http")) file = String(M5_SERVER_PATH) + file;
    if (fid.isEmpty()) fileAddr = file;

    String manifestName = jsonOptString(detail["name"]);
    if (!manifestName.isEmpty()) installedName = manifestName;

    return installFirmwareDynamic(
        fileAddr, file, appCopySize, appPartitionSize, appOffset, nb, dataPartitions, installedName,
        rebootAfter
    );
}

bool installFirmware(
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool nb,
    std::vector<LauncherInstallDataPartition> &dataPartitions, String installedName,
    bool askSpiffs, bool rebootAfter
) {
    if (!ensureWifiConnected()) return false;

    String fileAddr = launcherBuildSourceUrl(fid, file, true);
    if (!file.startsWith("http")) file = String(M5_SERVER_PATH) + file;
    if (fid.isEmpty()) fileAddr = file;

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
                "This app includes a SPIFFS data image. Install it too?",
                "Include"
            );
            if (!copySpiffs) spiffsIt->copySize = 0;
        }
    }

    return installFirmwareDynamic(
        fileAddr, file, app_size, app_size, app_offset, nb, dataPartitions, installedName, rebootAfter
    );
}

bool installExtFirmware(const String &url, bool rebootAfter) {
    if (!url.startsWith("http")) {
        launcherUiError("URL must start with http:// or https://");
        return false;
    }
    if (!ensureWifiConnected()) return false;

    launcherUiBeginProgress("Install from URL", "Connecting to server...", 0, true);

    size_t fileSize = 0;
    bool nb = true;
    std::vector<LauncherInstallDataPartition> dataPartitions;
    uint8_t bytes[32];
    uint8_t buff[kExtRangeBufSize];
    memset(buff, 0, sizeof(buff));

    LauncherHttpResponse response;
    RangeBufferContext range = {buff, sizeof(buff), 0};
    if (!launcherHttpGetRange(url.c_str(), 32768, sizeof(buff), rangeBufferCb, &range, &response) ||
        response.status != 206) {
        if (launcherUiIsCancelled()) {
            launcherUiError("Install cancelled.", "Cancelled");
        } else {
            launcherUiError("Could not reach binary at that URL");
        }
        return false;
    }
    if (launcherUiIsCancelled()) {
        launcherUiError("Install cancelled.", "Cancelled");
        return false;
    }
    if (!launcherParseContentRangeTotal(response.content_range, fileSize)) {
        launcherUiError("Server did not report file size");
        return false;
    }

    launcherUiStep("Inspecting app binary...", -1, "");

    size_t partitionSize = 0;
    size_t partitionOffset = 0x10000;
    if (buff[0] == 0xAA) {
        nb = false;
        for (int i = 0x0; i <= 0x1A0 && (i + 32) <= static_cast<int>(sizeof(buff)); i += 0x20) {
            memcpy(bytes, &buff[i], 32);

            if (bytes[3] == 0x00 || (bytes[3] >= 0x10 && bytes[3] <= 0x1F)) {
                if (bytes[0x0A] > 0 && partitionSize == 0) {
                    partitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                    partitionOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                }
            }
            if (bytes[3] == 0x81) {
                char labelBuf[17] = {0};
                memcpy(labelBuf, bytes + 12, 16);
                if (labelBuf[0] == '\0') continue;
                LauncherInstallDataPartition dp;
                dp.subtype = 0x81;
                dp.label = String(labelBuf);
                dp.sourceOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                uint32_t declaredSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                LauncherPartitionPayloadPlan payload =
                    launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, declaredSize);
                dp.partitionSize = payload.partitionSize;
                dp.copySize = payload.copySize;
                dataPartitions.push_back(dp);
            }
            if (bytes[3] == 0x82 || bytes[3] == 0x83) {
                char labelBuf[17] = {0};
                memcpy(labelBuf, bytes + 12, 16);
                if (labelBuf[0] == '\0') continue;
                LauncherInstallDataPartition dp;
                dp.subtype = bytes[3];
                dp.label = String(labelBuf);
                dp.sourceOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                dp.partitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                dp.copySize = dp.partitionSize;
                dataPartitions.push_back(dp);
            }
        }
        if (fileSize < partitionOffset + partitionSize) {
            partitionSize = fileSize > partitionOffset ? fileSize - partitionOffset : 0;
        }
        for (auto &dp : dataPartitions) {
            if (dp.subtype != 0x81 && fileSize < dp.sourceOffset + dp.copySize) {
                dp.copySize = fileSize > dp.sourceOffset ? fileSize - dp.sourceOffset : 0;
            }
        }
    }

    String extName = launcherAppNameFromFile(url);
    if (extName.isEmpty()) extName = "External App";
    return installFirmware(
        "", url, static_cast<uint32_t>(partitionSize), static_cast<uint32_t>(partitionOffset), nb,
        dataPartitions, extName, true, rebootAfter
    );
}

void saveDownloadedFirmware(const String &folder, const String &fid, const String &version) {
    if (fid.isEmpty() || version.isEmpty()) return;
    if (!setupSdCard()) return;

    String path = folder;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith("/")) path += "/";
    path += "downloaded.json";

    JsonDocument dwnDoc;
    File f = SD.open(path, FILE_READ);
    if (f) {
        deserializeJson(dwnDoc, f);
        f.close();
    }
    JsonArray arr = dwnDoc.as<JsonArray>();
    if (arr.isNull()) {
        dwnDoc.clear();
        arr = dwnDoc.to<JsonArray>();
    }
    JsonObject obj;
    if (arr.size() > 0) obj = arr[0].as<JsonObject>();
    if (obj.isNull()) obj = arr.add<JsonObject>();
    obj[fid] = version;

    f = SD.open(path, FILE_WRITE);
    if (f) {
        serializeJson(dwnDoc, f);
        f.close();
    }
}

bool downloadFirmware(
    const String &fid, String fileUrl, String fileName, String folder, const String &version
) {
    if (!ensureWifiConnected()) return false;
    if (!setupSdCard()) {
        launcherUiError("SD card not found. Insert a FAT32 SD card and try again.", "SD Card Required");
        return false;
    }

    launcherUiBeginProgress("Download to SD", "Connecting to server...", 0, true);
    String fileAddr = launcherBuildSourceUrl(fid, fileUrl, true);
    fileName = launcherSanitizeFilename(fileName);

    if (!folder.endsWith("/")) folder += "/";
    if (!folder.startsWith("/")) folder = "/" + folder;
    String folderName = folder.substring(0, folder.length() - 1);
    if (folderName.length() > 1 && !SD.exists(folderName)) {
        launcherUiStep("Preparing SD folder...", -1, folderName.c_str());
        if (!SD.mkdir(folderName)) {
            launcherUiError("Could not create folder: " + folderName);
            return false;
        }
    }
    String filePath = folder + fileName + ".bin";

    if (!fid.isEmpty() && !version.isEmpty()) {
        launcherUiStep("Checking app package...", -1, "");
        JsonDocument detail(launcherJsonAllocator());
        String infoUrl =
            "https://api.launcherhub.net/firmwares?fid=" + fid + "&version=" + launcherEncodeQueryValue(version);
        if (getInfo(infoUrl, detail)) {
            if (launcherUiIsCancelled()) {
                launcherUiError("Download cancelled.", "Cancelled");
                return false;
            }
            JsonObject install = detail["version"]["install"].as<JsonObject>();
            JsonObject sources = install["sources"].as<JsonObject>();
            bool hasBootAndParts = !jsonOptString(sources["bootloader"]).isEmpty() &&
                                   !jsonOptString(sources["partitions"]).isEmpty();
            bool hasSeparateData = !jsonOptString(sources["data"]).isEmpty();
            if (!sources.isNull() && (hasBootAndParts || hasSeparateData)) {
                return downloadSplitFirmware(fid, install, filePath, folder, version);
            }
        }
    }

    if (launcherUiIsCancelled()) {
        launcherUiError("Download cancelled.", "Cancelled");
        return false;
    }

    int tries = 0;
    LauncherHttpResponse response;
    bool ok = false;
    size_t sdSize = 0;

    while (tries < 2) {
        File file = SD.open(filePath, FILE_WRITE);
        if (!file) {
            launcherUiError("Could not create file on SD");
            return false;
        }

        FileDownloadContext download = {&file, 0, 0, 0, &response, "Downloading app to SD..."};
        String hwid = wifiMacString();
        launcherUiStep("Downloading app to SD...", -1, "Connecting...");
        ok = launcherHttpGetStream(fileAddr.c_str(), fileDownloadCb, &download, &response, "HWID", hwid.c_str());
        file.flush();
        file.close();

        launcherDelayMs(40);
        File checkFile = SD.open(filePath, FILE_READ);
        sdSize = checkFile ? checkFile.size() : 0;
        if (checkFile) checkFile.close();

        if (!launcherUiIsCancelled() && (!ok || sdSize <= kExtRangeBufSize) && tries == 0) {
            tries++;
            SD.remove(filePath);
            if (response.status >= 300 && response.status < 400 && fileAddr.indexOf("/download?") >= 0) {
                fileAddr.replace("/download?", "/proxy?");
                launcherConsolePrintf("Redirect not followed, falling back to /proxy for SD download\n");
                launcherUiStep("Retrying via mirror...", -1, "");
            } else {
                launcherUiStep("Retrying download...", -1, "");
            }
            continue;
        }
        break;
    }

    ok = ok && !launcherUiIsCancelled() &&
         !(response.content_length > 0 && sdSize != static_cast<size_t>(response.content_length));
    if (!ok) {
        SD.remove(filePath);
        launcherUiError(launcherUiIsCancelled() ? "Download cancelled." : "Download failed.",
                        launcherUiIsCancelled() ? "Cancelled" : "Error");
        return false;
    }

    launcherUiStep("Verifying SD file...", 100, "");
    saveDownloadedFirmware(folder, fid, version);
    s_lastDownloadedPath = filePath;
    String detail = launcherFormatDownloadSavedDetail(filePath, sdSize);
    launcherUiFinishWithPrimaryAction(
        true, "Saved to SD", detail.c_str(), "Install Now", installLastDownloadedSdCb, "Done"
    );
    return true;
}

bool checkForUpdates(String *errorOut) {
    if (!ensureWifiConnected()) {
        if (errorOut) *errorOut = "Wi-Fi not connected";
        return false;
    }
    if (!setupSdCard()) {
        if (errorOut) *errorOut = "SD card not found";
        return false;
    }

    String path = g_launcherDwnPath;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith("/")) path += "/";
    path += "downloaded.json";

    File f = SD.open(path, FILE_READ);
    if (!f) {
        if (errorOut) *errorOut = "No SD downloads recorded yet";
        return false;
    }
    String body;
    while (f.available()) body += static_cast<char>(f.read());
    f.close();

    if (body.isEmpty() || body == "[]" || body == "[{}]") {
        if (errorOut) *errorOut = "No SD downloads recorded yet";
        return false;
    }

    String response;
    LauncherHttpResponse httpResp;
    bool ok = launcherHttpPost(
        "https://api.launcherhub.net/updateList", body.c_str(), body.length(), response, 65536, &httpResp
    );
    if (!ok || response.isEmpty()) {
        if (errorOut) *errorOut = "Update check failed";
        return false;
    }

    g_launcherHubDoc.clear();
    JsonDocument filter;
    buildFirmwareListFilter(filter);
    DeserializationError err =
        deserializeJson(g_launcherHubDoc, response, DeserializationOption::Filter(filter));
    if (err) {
        if (errorOut) *errorOut = "Invalid server response";
        return false;
    }

    g_launcherTotalFirmware = g_launcherHubDoc["total"] | 0;
    if (g_launcherTotalFirmware == 0) {
        if (errorOut) *errorOut = "All downloaded apps are up to date";
        return false;
    }

    g_launcherHubDoc["page_size"] = g_launcherTotalFirmware;
    g_launcherHubDoc["page"] = 1;
    g_launcherNumPages = 1;
    g_launcherCurrentPage = 1;
    return true;
}

bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label) {
    if (!ensureWifiConnected()) return false;

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition) {
        launcherUiError(String("Partition not found: ") + label);
        return false;
    }

    launcherUiBeginProgress("Installing Data", (String("Installing FAT: ") + label + "...").c_str());
    RawHttpUpdateContext fatUpdate = {
        partition->address, partition->size, offset, size, 0, false, false, "Installing FAT data..."
    };
    bool ok = launcherHttpGetStream(file.c_str(), launcherRawUpdateHttpCb, &fatUpdate) &&
              fatUpdate.written == size && launcherRawUpdateEnd();
    if (ok) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(partition->address, partition->size, &patchError)) {
            launcherConsolePrintf("LittleFS patch failed after FAT OTA label=%s: %s\n", label, patchError.c_str());
            ok = false;
        }
    }

    if (ok) launcherUiFinish(true, "Data Installed", label);
    else launcherUiError(String("FAT install failed: ") + label);
    return ok;
}
