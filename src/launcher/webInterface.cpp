#include "webInterface.h"

#ifndef LAUNCHER_WEBUI_HOST_TEST

#include "ble_bonds.h"
#include "idf_update.h"
#include "idf_web_server.h"
#include "install_shared.h"
#include "launcher_app_registry.h"
#include "launcher_platform.h"
#include "launcher_ui_bridge.h"
#include "littlefs_patch.h"
#include "onlineLauncher.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "sd_functions.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <vector>

namespace {

constexpr uint16_t kWebPort = 80;
constexpr const char *kApSsid = "Pixel-CYD-Store";
constexpr size_t kUploadChunkSize = 4096;
constexpr size_t kUploadChunkFallback = 1024;

struct WebParamEntry {
    String key;
    String value;
};

struct WebParamMap {
    std::vector<WebParamEntry> values;

    bool has(const char *key) const {
        for (const WebParamEntry &entry : values) {
            if (entry.key == key) return true;
        }
        return false;
    }
    String get(const char *key) const {
        for (const WebParamEntry &entry : values) {
            if (entry.key == key) return entry.value;
        }
        return "";
    }
    void set(const String &key, const String &value) {
        for (WebParamEntry &entry : values) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
        }
        values.push_back({key, value});
    }
};

struct WebInstallStage {
    bool appImage = false;
    uint8_t subtype = 0xFF;
    uint32_t sourceOffset = 0;
    uint32_t copySize = 0;
    LauncherPartitionEntry entry;
    uint32_t written = 0;
    bool started = false;
};

struct WebInstallContext {
    bool active = false;
    String sourceName;
    LauncherPartitionTable table;
    LauncherPartitionEntry appEntry;
    std::vector<WebInstallStage> stages;
    size_t currentStage = 0;
    uint32_t sourcePos = 0;
    uint32_t totalCopySize = 0;
    uint32_t totalWritten = 0;
};

portMUX_TYPE s_statusMux = portMUX_INITIALIZER_UNLOCKED;
LauncherWebUiStatus s_status;
httpd_handle_t s_server = nullptr;
bool s_startedAp = false;
bool s_pendingReboot = false;
bool s_pendingAppsSync = false;
char s_pendingSyncLabel[16] = {};
char s_pendingSdInstall[96] = {};

WebInstallContext s_installCtx;
bool s_otaArmed = false;
int s_otaCommand = 0;
size_t s_otaFileSize = 0;
String s_uploadFolder = "/downloads";

uint8_t *s_scratch = nullptr;
size_t s_scratchSize = 0;

uint8_t *acquireScratch(size_t size) {
    if (s_scratch && s_scratchSize >= size) return s_scratch;
    free(s_scratch);
    s_scratch = static_cast<uint8_t *>(malloc(size));
    s_scratchSize = s_scratch ? size : 0;
    return s_scratch;
}

void releaseScratch() {
    free(s_scratch);
    s_scratch = nullptr;
    s_scratchSize = 0;
}

void setStatus(LauncherWebUiState state,
               const char *text,
               uint8_t pct = 0,
               uint32_t done = 0,
               uint32_t total = 0,
               bool bumpEvent = false) {
    taskENTER_CRITICAL(&s_statusMux);
    s_status.state = state;
    s_status.progressPct = pct > 100 ? 100 : pct;
    s_status.bytesDone = done;
    s_status.bytesTotal = total;
    if (text) {
        strncpy(s_status.statusText, text, sizeof(s_status.statusText) - 1);
        s_status.statusText[sizeof(s_status.statusText) - 1] = '\0';
    }
    if (bumpEvent) s_status.eventSeq++;
    taskEXIT_CRITICAL(&s_statusMux);
}

void recordInstalledApp(const char *label, const char *name) {
    taskENTER_CRITICAL(&s_statusMux);
    strncpy(s_status.lastInstalledLabel, label ? label : "", sizeof(s_status.lastInstalledLabel) - 1);
    s_status.lastInstalledLabel[sizeof(s_status.lastInstalledLabel) - 1] = '\0';
    strncpy(s_status.lastInstalledName, name ? name : "", sizeof(s_status.lastInstalledName) - 1);
    s_status.lastInstalledName[sizeof(s_status.lastInstalledName) - 1] = '\0';
    strncpy(s_pendingSyncLabel, label ? label : "", sizeof(s_pendingSyncLabel) - 1);
    s_pendingSyncLabel[sizeof(s_pendingSyncLabel) - 1] = '\0';
    s_pendingAppsSync = true;
    s_status.eventSeq++;
    taskEXIT_CRITICAL(&s_statusMux);
}

void clearWebInstallContext() { s_installCtx = WebInstallContext(); }

String humanReadableSize(uint64_t bytes) {
    if (bytes < 1024) return String(static_cast<unsigned long>(bytes)) + " B";
    if (bytes < (1024ULL * 1024ULL)) return String(static_cast<unsigned long>((bytes + 512) / 1024)) + " KB";
    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return String(buf);
}

String urlDecode(const String &input) {
    String out;
    out.reserve(input.length());
    for (int i = 0; i < static_cast<int>(input.length()); ++i) {
        char c = input[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < static_cast<int>(input.length())) {
            char hex[3] = {input[i + 1], input[i + 2], 0};
            out += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

void parseQueryString(const String &qs, WebParamMap &map) {
    int start = 0;
    while (start < static_cast<int>(qs.length())) {
        int amp = qs.indexOf('&', start);
        String pair = amp >= 0 ? qs.substring(start, amp) : qs.substring(start);
        int eq = pair.indexOf('=');
        if (eq > 0) {
            map.set(urlDecode(pair.substring(0, eq)), urlDecode(pair.substring(eq + 1)));
        } else if (!pair.isEmpty()) {
            map.set(urlDecode(pair), "");
        }
        if (amp < 0) break;
        start = amp + 1;
    }
}

String queryValue(httpd_req_t *req, const char *key) {
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0) return "";
    String qs;
    qs.reserve(len + 1);
    std::vector<char> buf(len + 1, 0);
    if (httpd_req_get_url_query_str(req, buf.data(), buf.size()) != ESP_OK) return "";
    WebParamMap params;
    parseQueryString(String(buf.data()), params);
    return params.get(key);
}

String headerValue(httpd_req_t *req, const char *name) {
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0) return "";
    std::vector<char> buf(len + 1, 0);
    if (httpd_req_get_hdr_value_str(req, name, buf.data(), buf.size()) != ESP_OK) return "";
    return String(buf.data());
}

bool receiveBody(httpd_req_t *req, String &out, size_t maxBytes = 8192) {
    if (req->content_len == 0) return true;
    if (req->content_len > maxBytes) return false;
    out.reserve(req->content_len + 1);
    char buf[512];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        int r = httpd_req_recv(req, buf, want);
        if (r <= 0) return false;
        out.concat(buf, static_cast<unsigned int>(r));
        remaining -= static_cast<size_t>(r);
    }
    return true;
}

WebParamMap readParams(httpd_req_t *req) {
    WebParamMap params;
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        std::vector<char> buf(qlen + 1, 0);
        if (httpd_req_get_url_query_str(req, buf.data(), buf.size()) == ESP_OK) {
            parseQueryString(String(buf.data()), params);
        }
    }
    if (req->method == HTTP_POST && req->content_len > 0 && req->content_len <= 8192) {
        String ct = headerValue(req, "Content-Type");
        if (ct.indexOf("multipart/form-data") < 0) {
            String body;
            if (receiveBody(req, body, 8192)) {
                parseQueryString(body, params);
            }
        }
    }
    return params;
}

void sendText(httpd_req_t *req, int code, const char *mime, const String &body) {
    if (code == 400) httpd_resp_set_status(req, "400 Bad Request");
    else if (code == 404) httpd_resp_set_status(req, "404 Not Found");
    else if (code == 500) httpd_resp_set_status(req, "500 Internal Server Error");
    else httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, mime);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, body.c_str(), body.length());
}

void sendText(httpd_req_t *req, const char *mime, const String &body) {
    sendText(req, 200, mime, body);
}

void drainBody(httpd_req_t *req, size_t remaining, uint8_t *buff, size_t cap) {
    while (remaining > 0) {
        size_t want = remaining < cap ? remaining : cap;
        int r = httpd_req_recv(req, reinterpret_cast<char *>(buff), want);
        if (r <= 0) break;
        remaining -= static_cast<size_t>(r);
    }
}

// --- Vendored Launcher Dynamic Web Install Engine ---

void addWebInstallStage(const WebInstallStage &stage) {
    size_t insertAt = s_installCtx.stages.size();
    while (insertAt > 0 && s_installCtx.stages[insertAt - 1].sourceOffset > stage.sourceOffset) {
        --insertAt;
    }
    s_installCtx.stages.insert(s_installCtx.stages.begin() + insertAt, stage);
    s_installCtx.totalCopySize += stage.copySize;
}

bool prepareWebDataPartition(
    LauncherPartitionTable &table, uint8_t subtype, const String &label, uint32_t declaredSize,
    uint32_t copySize, LauncherPartitionEntry &entry, String &error
) {
    if (subtype == 0x81) {
        LauncherPartitionPayloadPlan payload =
            launcherPartitionFatPayloadPlan(label.c_str(), declaredSize, copySize);
        return launcherPartitionFindOrCreateData(
            table, subtype, label.c_str(), payload.partitionSize, entry, error
        );
    }
    LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, label.c_str());
    bool useRemaining = false;
    uint32_t requestedSize = declaredSize > 0 ? declaredSize : copySize;
    if (label == "spiffs" && declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
        useRemaining = true;
        requestedSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
    }
    if (existing) {
        if (!existing->isData() || existing->subtype != subtype) {
            error = String("Partition ") + label + " incompatible";
            return false;
        }
        if (useRemaining) {
            uint32_t oldOffset = existing->offset;
            if (!launcherPartitionRemoveEntryByOffset(table, oldOffset)) {
                error = String("Could not resize ") + label;
                return false;
            }
            return launcherPartitionCreateDataInLargestFreeRange(table, subtype, label.c_str(), entry, error);
        }
        if (existing->size < requestedSize) {
            error = String("Partition ") + label + " too small";
            return false;
        }
        entry = *existing;
        return true;
    }
    if (useRemaining) {
        return launcherPartitionCreateDataInLargestFreeRange(table, subtype, label.c_str(), entry, error);
    }
    return launcherPartitionFindOrCreateData(table, subtype, label.c_str(), requestedSize, entry, error);
}

bool parseWebInstallManifest(const String &manifestJson, size_t uploadSize, String &error) {
    JsonDocument manifest;
    if (deserializeJson(manifest, manifestJson)) {
        error = "Bad manifest";
        return false;
    }
    JsonArray parts = manifest["parts"].as<JsonArray>();
    if (parts.isNull() || parts.size() == 0) {
        error = "Missing parts";
        return false;
    }

    clearWebInstallContext();
    if (!launcherPartitionReadCurrent(s_installCtx.table, &error)) return false;
    s_installCtx.sourceName = manifest["sourceName"].as<String>();
    s_installCtx.totalCopySize = 0;

    bool hasApp = false;
    uint32_t appOffset = 0;
    uint32_t appSize = 0;
    std::vector<LauncherInstallDataPartition> dataPartitions;

    for (JsonObject part : parts) {
        String kind = part["kind"].as<String>();
        uint32_t sourceOffset = part["sourceOffset"] | 0;
        uint32_t copySize = part["copySize"] | 0;
        uint32_t declaredSize = part["declaredSize"] | copySize;
        uint8_t subtype = static_cast<uint8_t>(part["subtype"] | 0xFF);
        bool isSpiffsLike = (subtype == 0x82 || subtype == 0x83);
        if (copySize == 0 && !(isSpiffsLike && declaredSize > 0)) continue;
        if (sourceOffset > uploadSize || copySize > uploadSize - sourceOffset) {
            error = "Manifest range exceeds file";
            return false;
        }
        if (kind == "app") {
            hasApp = true;
            appOffset = sourceOffset;
            appSize = copySize;
            continue;
        }
        String label = part["label"].as<String>();
        if (label.isEmpty()) label = launcherInstallDefaultDataLabel(subtype);
        LauncherInstallDataPartition dp;
        dp.subtype = subtype;
        dp.label = label;
        dp.sourceOffset = sourceOffset;
        dp.copySize = copySize;
        if (subtype == 0x81) {
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(label.c_str(), declaredSize, copySize);
            dp.partitionSize = payload.partitionSize;
            dp.copySize = payload.copySize;
        } else if (isSpiffsLike) {
            dp.partitionSize = (declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) ? declaredSize
                                                                             : LAUNCHER_DEFAULT_SPIFFS_SIZE;
        } else {
            continue;
        }
        dataPartitions.push_back(dp);
    }

    if (!hasApp || appSize == 0) {
        error = "Missing app part";
        return false;
    }

    String appLabel =
        launcherInstallNextAppLabel(s_installCtx.table, s_installCtx.sourceName, "", "WebUI App");

    // Non-interactive allocation inside httpd task (never invoke LVGL modals from httpd!)
    if (!launcherPartitionCreateOtaApp(
            s_installCtx.table, appSize, appLabel.c_str(), &s_installCtx.appEntry, &error
        )) {
        return false;
    }
    for (auto &dp : dataPartitions) {
        if (!prepareWebDataPartition(
                s_installCtx.table, dp.subtype, dp.label, dp.partitionSize, dp.copySize, dp.entry, error
            )) {
            return false;
        }
        dp.hasEntry = true;
    }
    if (!launcherPartitionValidate(s_installCtx.table, &error)) return false;

    WebInstallStage appStage;
    appStage.appImage = true;
    appStage.subtype = 0x00;
    appStage.sourceOffset = appOffset;
    appStage.copySize = appSize;
    appStage.entry = s_installCtx.appEntry;
    addWebInstallStage(appStage);

    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry || dp.copySize == 0) continue;
        WebInstallStage stage;
        stage.appImage = false;
        stage.subtype = dp.entry.subtype;
        stage.sourceOffset = dp.sourceOffset;
        stage.copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        stage.entry = dp.entry;
        addWebInstallStage(stage);
    }
    s_installCtx.active = !s_installCtx.stages.empty();
    return s_installCtx.active;
}

bool prepareWebInstallContext(int commandValue, size_t uploadSize, const WebParamMap &params, String &error) {
    if (params.has("manifest")) return parseWebInstallManifest(params.get("manifest"), uploadSize, error);

    clearWebInstallContext();
    if (!launcherPartitionReadCurrent(s_installCtx.table, &error)) return false;

    s_installCtx.sourceName = params.get("sourceName");
    if (s_installCtx.sourceName.isEmpty()) s_installCtx.sourceName = "WebUI App";

    WebInstallStage stage;
    stage.appImage = (commandValue == LAUNCHER_UPDATE_COMMAND_FLASH);
    stage.sourceOffset = 0;
    stage.copySize = static_cast<uint32_t>(uploadSize);

    if (stage.appImage) {
        String appLabel =
            launcherInstallNextAppLabel(s_installCtx.table, s_installCtx.sourceName, "", "WebUI App");
        if (!launcherPartitionCreateOtaApp(
                s_installCtx.table, stage.copySize, appLabel.c_str(), &s_installCtx.appEntry, &error
            )) {
            return false;
        }
        stage.entry = s_installCtx.appEntry;
    } else {
        uint8_t subtype = params.has("subtype") ? static_cast<uint8_t>(params.get("subtype").toInt()) : 0x82;
        String label = params.get("label");
        if (label.isEmpty()) label = launcherInstallDefaultDataLabel(subtype);
        uint32_t declaredSize = params.has("declaredSize")
                                    ? static_cast<uint32_t>(params.get("declaredSize").toInt())
                                    : stage.copySize;
        if (!prepareWebDataPartition(
                s_installCtx.table, subtype, label, declaredSize, stage.copySize, stage.entry, error
            )) {
            return false;
        }
        stage.subtype = subtype;
    }

    if (!launcherPartitionValidate(s_installCtx.table, &error)) return false;
    s_installCtx.totalCopySize = 0;
    addWebInstallStage(stage);
    s_installCtx.active = true;
    return true;
}

bool beginWebInstallStage(WebInstallStage &stage) {
    if (launcherRawUpdateBegin(stage.entry.offset, stage.entry.size, stage.copySize, stage.appImage)) {
        stage.started = true;
        return true;
    }
    return false;
}

bool finishWebInstallStage(WebInstallStage &stage) {
    if (stage.started) {
        if (!launcherRawUpdateEnd()) return false;
        stage.started = false;
    }
    if (!stage.appImage) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(stage.entry, &patchError)) {
            return false;
        }
    }
    return true;
}

bool writeWebInstallData(const uint8_t *data, size_t len) {
    size_t consumed = 0;
    while (consumed < len && s_installCtx.currentStage < s_installCtx.stages.size()) {
        WebInstallStage &stage = s_installCtx.stages[s_installCtx.currentStage];
        uint32_t chunkStart = s_installCtx.sourcePos + static_cast<uint32_t>(consumed);
        uint32_t stageStart = stage.sourceOffset + stage.written;
        uint32_t stageEnd = stage.sourceOffset + stage.copySize;

        if (chunkStart < stageStart) {
            size_t skip = std::min<size_t>(len - consumed, stageStart - chunkStart);
            consumed += skip;
            continue;
        }

        if (!stage.started && !beginWebInstallStage(stage)) return false;

        size_t writeLen = std::min<size_t>(len - consumed, stageEnd - stageStart);
        if (launcherRawUpdateWrite(data + consumed, writeLen) != writeLen) return false;

        stage.written += static_cast<uint32_t>(writeLen);
        s_installCtx.totalWritten += static_cast<uint32_t>(writeLen);
        consumed += writeLen;

        uint8_t pct = s_installCtx.totalCopySize > 0
                          ? static_cast<uint8_t>((static_cast<uint64_t>(s_installCtx.totalWritten) * 100ULL) /
                                                 s_installCtx.totalCopySize)
                          : 0;
        String msg = String("Flashing ") + launcherAppNameFromFile(s_installCtx.sourceName) + "...";
        setStatus(LauncherWebUiState::Flashing, msg.c_str(), pct, s_installCtx.totalWritten,
                  s_installCtx.totalCopySize);

        if (stage.written == stage.copySize) {
            if (!finishWebInstallStage(stage)) return false;
            s_installCtx.currentStage++;
        }
    }
    s_installCtx.sourcePos += static_cast<uint32_t>(len);
    return true;
}

bool finalizeWebInstall(String &errorOut) {
    if (s_installCtx.currentStage != s_installCtx.stages.size() ||
        s_installCtx.totalWritten != s_installCtx.totalCopySize) {
        errorOut = "Incomplete flash transfer";
        clearWebInstallContext();
        return false;
    }

    String tableError;
    if (!launcherPartitionWriteGeneratedTable(s_installCtx.table, &tableError)) {
        errorOut = tableError.isEmpty() ? "Partition table write failed" : tableError;
        clearWebInstallContext();
        return false;
    }
    if (s_installCtx.appEntry.offset != 0 && g_launcherBootToApp) {
        launcherPartitionSetOtaBoot(s_installCtx.table, s_installCtx.appEntry.subtype, &tableError);
    }

    // Save metadata directly (without calling LVGL from the httpd task!) and defer Home Screen sync.
    String appLabel = String(s_installCtx.appEntry.label);
    String appName = launcherInstallAppDisplayName(s_installCtx.sourceName, "");
    if (appName.isEmpty()) appName = appLabel.isEmpty() ? String("WebUI App") : appLabel;

    if (s_installCtx.appEntry.offset != 0) {
        std::vector<String> fatLabels;
        String spiffsLabel;
        for (const WebInstallStage &stage : s_installCtx.stages) {
            if (stage.appImage) continue;
            if (stage.subtype == 0x81) fatLabels.push_back(String(stage.entry.label));
            else if ((stage.subtype == 0x82 || stage.subtype == 0x83) && spiffsLabel.isEmpty()) {
                spiffsLabel = String(stage.entry.label);
            }
        }
        launcherPruneAppRegistry(s_installCtx.table);
        launcherBleBondsSwitchTo(appLabel.c_str());

        LauncherAppMetadata meta;
        meta.name = appName;
        meta.label = appLabel;
        meta.fatLabels = fatLabels;
        meta.spiffsLabel = spiffsLabel;
        meta.appNum = launcherGenerateAppNum(s_installCtx.sourceName);
        launcherSaveAppMetadata(meta);
    }

    g_launcherLastInstalledApp = appName;
    launcherUiSaveState();
    clearWebInstallContext();

    String okMsg = String("Installed ") + appName + "!";
    setStatus(LauncherWebUiState::Success, okMsg.c_str(), 100, 0, 0, true);
    recordInstalledApp(appLabel.c_str(), appName.c_str());
    return true;
}

// --- Multipart Stream Parser (handles both OTA flash and SD file upload) ---

bool extractBoundary(httpd_req_t *req, String &boundaryOut) {
    String ct = headerValue(req, "Content-Type");
    int idx = ct.indexOf("boundary=");
    if (idx < 0) return false;
    String b = ct.substring(idx + 9);
    int semi = b.indexOf(';');
    if (semi >= 0) b = b.substring(0, semi);
    b.trim();
    if (b.startsWith("\"") && b.endsWith("\"") && b.length() >= 2) {
        b = b.substring(1, b.length() - 1);
    }
    if (b.isEmpty()) return false;
    boundaryOut = "--" + b;
    return true;
}

String extractFilenameFromHeaders(const String &hdr) {
    int fnIdx = hdr.indexOf("filename=\"");
    if (fnIdx < 0) return "";
    fnIdx += 10;
    int endIdx = hdr.indexOf('"', fnIdx);
    if (endIdx < 0) return "";
    String name = hdr.substring(fnIdx, endIdx);
    int slash = name.lastIndexOf('/');
    int bslash = name.lastIndexOf('\\');
    int sep = slash > bslash ? slash : bslash;
    if (sep >= 0) name = name.substring(sep + 1);
    return name;
}

bool streamUploadRequest(httpd_req_t *req, bool flashMode) {
    String boundary;
    bool isMultipart = extractBoundary(req, boundary);

    size_t cap = kUploadChunkSize;
    uint8_t *buff = acquireScratch(cap);
    if (!buff) {
        cap = kUploadChunkFallback;
        buff = acquireScratch(cap);
    }
    if (!buff) {
        setStatus(LauncherWebUiState::Error, "Out of memory", 0, 0, 0, true);
        sendText(req, 500, "text/plain", "Out of memory");
        return false;
    }

    // Raw binary body upload (no multipart framing) for direct flash
    if (!isMultipart && flashMode && s_installCtx.active) {
        size_t remaining = req->content_len;
        while (remaining > 0) {
            size_t want = remaining < cap ? remaining : cap;
            int r = httpd_req_recv(req, reinterpret_cast<char *>(buff), want);
            if (r <= 0) {
                clearWebInstallContext();
                setStatus(LauncherWebUiState::Error, "Upload connection lost", 0, 0, 0, true);
                sendText(req, 500, "text/plain", "Receive failed");
                return false;
            }
            if (!writeWebInstallData(buff, static_cast<size_t>(r))) {
                clearWebInstallContext();
                setStatus(LauncherWebUiState::Error, "Flash write failed", 0, 0, 0, true);
                sendText(req, 500, "text/plain", "Flash write failed");
                return false;
            }
            remaining -= static_cast<size_t>(r);
            esp_task_wdt_reset();
        }
        String finErr;
        if (!finalizeWebInstall(finErr)) {
            setStatus(LauncherWebUiState::Error, finErr.c_str(), 0, 0, 0, true);
            sendText(req, 500, "text/plain", finErr);
            return false;
        }
        sendText(req, 200, "text/plain", "OK");
        return true;
    }

    if (!isMultipart) {
        sendText(req, 400, "text/plain", "Expected multipart/form-data");
        return false;
    }

    String crlfBoundary = "\r\n" + boundary;
    const size_t boundaryLen = crlfBoundary.length();
    size_t remaining = req->content_len;
    size_t len = 0;

    bool headersParsed = false;
    bool inFile = false;
    File sdFile;
    String activePath;
    String fileName;
    size_t written = 0;
    size_t totalHint = req->content_len;
    String error;

    while (true) {
        if (!headersParsed) {
            // Find end of part headers (\r\n\r\n)
            int hdrEnd = -1;
            for (size_t i = 0; i + 3 < len; ++i) {
                if (buff[i] == '\r' && buff[i + 1] == '\n' && buff[i + 2] == '\r' && buff[i + 3] == '\n') {
                    hdrEnd = static_cast<int>(i);
                    break;
                }
            }
            if (hdrEnd >= 0) {
                String hdr;
                hdr.concat(reinterpret_cast<const char *>(buff), static_cast<unsigned int>(hdrEnd));
                fileName = extractFilenameFromHeaders(hdr);
                size_t consume = static_cast<size_t>(hdrEnd + 4);
                memmove(buff, buff + consume, len - consume);
                len -= consume;
                headersParsed = true;

                if (flashMode) {
                    if (!s_installCtx.active) {
                        error = "OTA not prepared";
                        break;
                    }
                    inFile = true;
                } else {
                    if (!setupSdCard()) {
                        error = "SD card not mounted";
                        break;
                    }
                    if (fileName.isEmpty()) fileName = "upload.bin";
                    String dir = s_uploadFolder.isEmpty() ? String("/downloads") : s_uploadFolder;
                    if (dir != "/" && !SD.exists(dir)) SD.mkdir(dir);
                    activePath = (dir == "/") ? ("/" + fileName) : (dir + "/" + fileName);
                    sdFile = SD.open(activePath, FILE_WRITE);
                    if (!sdFile) {
                        error = "Could not open SD file";
                        break;
                    }
                    inFile = true;
                    String msg = String("Uploading ") + fileName + "...";
                    setStatus(LauncherWebUiState::Uploading, msg.c_str(), 0, 0, totalHint);
                }
                continue;
            }
        } else if (inFile) {
            // Search for closing \r\n--boundary
            int found = -1;
            if (len >= boundaryLen) {
                for (size_t i = 0; i + boundaryLen <= len; ++i) {
                    if (memcmp(buff + i, crlfBoundary.c_str(), boundaryLen) == 0) {
                        found = static_cast<int>(i);
                        break;
                    }
                }
            }

            size_t safeWrite = 0;
            if (found >= 0) {
                safeWrite = static_cast<size_t>(found);
            } else if (len > boundaryLen + 4) {
                safeWrite = len - (boundaryLen + 4);
            }

            if (safeWrite > 0) {
                if (flashMode) {
                    if (!writeWebInstallData(buff, safeWrite)) {
                        error = "Flash write failed";
                        break;
                    }
                } else {
                    if (sdFile.write(buff, safeWrite) != safeWrite) {
                        error = "SD write failed";
                        break;
                    }
                    written += safeWrite;
                    uint8_t pct = totalHint > 0
                                      ? static_cast<uint8_t>(std::min<uint64_t>(
                                            100ULL, (static_cast<uint64_t>(written) * 100ULL) / totalHint))
                                      : 0;
                    String msg = String("Uploading ") + fileName + "...";
                    setStatus(LauncherWebUiState::Uploading, msg.c_str(), pct, written, totalHint);
                }
                memmove(buff, buff + safeWrite, len - safeWrite);
                len -= safeWrite;
                esp_task_wdt_reset();
            }

            if (found >= 0) {
                inFile = false;
                if (!flashMode && sdFile) {
                    sdFile.close();
                }
                break;
            }
        }

        if (remaining == 0) break;
        if (len >= cap) {
            error = "Malformed upload";
            break;
        }
        size_t want = cap - len;
        if (want > remaining) want = remaining;
        int r = httpd_req_recv(req, reinterpret_cast<char *>(buff) + len, want);
        if (r <= 0) {
            error = "Upload interrupted";
            break;
        }
        len += static_cast<size_t>(r);
        remaining -= static_cast<size_t>(r);
    }

    if (inFile && !flashMode) {
        if (sdFile) sdFile.close();
        if (!activePath.isEmpty()) SD.remove(activePath);
        if (error.isEmpty()) error = "Upload truncated";
    }
    if (remaining > 0) drainBody(req, remaining, buff, cap);

    if (!error.isEmpty()) {
        if (flashMode) clearWebInstallContext();
        setStatus(LauncherWebUiState::Error, error.c_str(), 0, 0, 0, true);
        sendText(req, 500, "text/plain", error);
        return false;
    }

    if (flashMode) {
        String finErr;
        if (!finalizeWebInstall(finErr)) {
            setStatus(LauncherWebUiState::Error, finErr.c_str(), 0, 0, 0, true);
            sendText(req, 500, "text/plain", finErr);
            return false;
        }
    } else {
        String msg = String("Saved ") + fileName + " to SD";
        setStatus(LauncherWebUiState::Success, msg.c_str(), 100, written, written, true);
    }

    sendText(req, 200, "text/plain", "OK");
    return true;
}

// --- Compact Material 3 Single-Page WebUI HTML ---

const char kIndexHtml[] PROGMEM = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pixel CYD App Store</title><style>
:root{--bg:#0e1117;--c:#1a1e28;--c2:#252a36;--p:#8ab4f8;--g:#81c995;--r:#f28b82;--t:#e8eaed;--s:#9aa0a6}
*{box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}
body{margin:0;background:var(--bg);color:var(--t);padding:14px;max-width:640px;margin:auto}
h1{font-size:20px;margin:0 0 12px;color:var(--p)}
h2{font-size:14px;margin:0 0 8px;color:var(--s);text-transform:uppercase;letter-spacing:.5px}
.card{background:var(--c);border-radius:16px;padding:14px;margin-bottom:12px;border:1px solid #2b3140}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
button{background:var(--p);color:#0e1117;border:0;border-radius:99px;padding:8px 14px;font-weight:600;cursor:pointer;font-size:13px}
button.sec{background:var(--c2);color:var(--t)}
button.ok{background:var(--g);color:#0e1117}
button.err{background:var(--r);color:#0e1117}
button:disabled{opacity:.5;cursor:default}
input[type=file]{width:100%;padding:10px;background:var(--c2);color:var(--t);border-radius:12px;border:1px dashed #4a5568;margin-bottom:10px}
.bar{height:10px;background:var(--c2);border-radius:99px;overflow:hidden;margin:8px 0}
.fill{height:100%;width:0;background:var(--p);transition:width .15s}
.st{font-size:12px;color:var(--s);min-height:18px}
.item{display:flex;justify-content:space-between;align-items:center;padding:8px 10px;background:var(--c2);border-radius:12px;margin-top:6px;gap:8px}
.item b{font-size:13px;word-break:break-all}
.muted{font-size:11px;color:var(--s)}
</style></head><body>
<h1>Pixel CYD App Store — Web Upload</h1>
<div class="card">
<h2>Upload &amp; Install App (.bin)</h2>
<input id="f" type="file" accept=".bin,*/*">
<div class="row">
<button id="bFlash" onclick="flashBin()">Install to Flash</button>
<button id="bSd" class="sec" onclick="uploadSd()">Upload to SD (/downloads)</button>
</div>
<div class="bar"><div id="fill" class="fill"></div></div>
<div id="st" class="st">Select a .bin firmware to flash directly or save to SD.</div>
</div>
<div class="card">
<div class="row" style="justify-content:space-between"><h2>Installed Apps</h2><button class="sec" onclick="loadInfo()">Refresh</button></div>
<div id="apps" class="muted">Loading...</div>
</div>
<div class="card">
<div class="row" style="justify-content:space-between"><h2>SD Card (<span id="curDir">/downloads</span>)</h2>
<div class="row"><button class="sec" onclick="loadFiles('/')">/</button><button class="sec" onclick="loadFiles('/downloads')">/downloads</button></div></div>
<div id="files" class="muted">Loading...</div>
</div>
<div class="row"><button class="err" onclick="reboot()">Reboot Device</button><span id="sdInfo" class="muted"></span></div>
<script>
const $=id=>document.getElementById(id);
let curDir='/downloads';
function setSt(t,p){$('st').textContent=t;if(p>=0)$('fill').style.width=p+'%';}
async function loadInfo(){
 try{
  const r=await fetch('/systeminfo');const d=await r.json();
  $('sdInfo').textContent=d.SD&&d.SD.total?('SD: '+d.SD.used+' / '+d.SD.total):'No SD mounted';
  const a=d.APPS||[];
  $('apps').innerHTML=a.length?a.map(x=>`<div class="item"><div><b>${x.name}</b><div class="muted">${x.label}</div></div><button class="ok" onclick="bootApp('${x.label}')">Launch</button></div>`).join(''):'<div class="muted">No Apps installed yet.</div>';
 }catch(e){$('apps').textContent='Could not load info';}
}
async function loadFiles(dir){
 curDir=dir||'/';$('curDir').textContent=curDir;
 try{
  const r=await fetch('/listfiles?folder='+encodeURIComponent(curDir));
  const txt=await r.text();const lines=txt.trim().split('\n').slice(1);
  if(!lines.length){$('files').innerHTML='<div class="muted">Empty folder or no SD card.</div>';return;}
  $('files').innerHTML=lines.map(l=>{
   const p=l.split(':');const ty=p[0],nm=p[1],sz=p[2]||'';
   const full=(curDir==='/'?'/':curDir+'/')+nm;
   if(ty==='Fo')return `<div class="item"><b>[DIR] ${nm}</b><button class="sec" onclick="loadFiles('${full}')">Open</button></div>`;
   const bin=nm.toLowerCase().endsWith('.bin');
   return `<div class="item"><div><b>${nm}</b><div class="muted">${sz}</div></div><div class="row">${bin?`<button onclick="instSd('${full}')">Install</button>`:''}<button class="sec" onclick="delFile('${full}')">Del</button></div></div>`;
  }).join('');
 }catch(e){$('files').textContent='SD unavailable';}
}
function xhrUpload(url,file,onDone){
 const fd=new FormData();fd.append('file',file,file.name);
 const x=new XMLHttpRequest();x.open('POST',url,true);
 x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);setSt('Transferring '+file.name+' ('+p+'%)...',p);}};
 x.onload=()=>{if(x.status===200){setSt('Done: '+file.name,100);onDone&&onDone();}else{setSt('Error: '+x.responseText,0);}};
 x.onerror=()=>setSt('Transfer failed',0);
 x.send(fd);
}
async function flashBin(){
 const f=$('f').files[0];if(!f){setSt('Pick a .bin file first.',0);return;}
 setSt('Preparing flash slot for '+f.name+'...',5);
 const q=`/OTA?command=0&size=${f.size}&dynamic=1&sourceName=${encodeURIComponent(f.name)}`;
 const r=await fetch(q,{method:'POST'});
 if(!r.ok){setSt('Flash prep failed: '+await r.text(),0);return;}
 xhrUpload('/OTAFILE',f,()=>loadInfo());
}
function uploadSd(){
 const f=$('f').files[0];if(!f){setSt('Pick a file first.',0);return;}
 xhrUpload('/',f,()=>loadFiles(curDir));
}
async function instSd(p){setSt('Starting SD install: '+p,-1);await fetch('/UPDATE?fileName='+encodeURIComponent(p),{method:'POST'});setSt('Installing on device screen...',100);}
async function delFile(p){if(!confirm('Delete '+p+'?'))return;await fetch('/file?action=delete&name='+encodeURIComponent(p));loadFiles(curDir);}
async function bootApp(l){await fetch('/bootapp?label='+encodeURIComponent(l),{method:'POST'});setSt('Launching '+l+'...',100);}
async function reboot(){await fetch('/reboot');setSt('Rebooting device...',100);}
loadInfo();loadFiles('/downloads');
</script></body></html>)rawliteral";

// --- HTTP Route Handlers ---

esp_err_t pingHandler(httpd_req_t *req) {
    sendText(req, "text/plain", "launcher-pong");
    return ESP_OK;
}

esp_err_t rootHandler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        s_otaArmed = false;
        clearWebInstallContext();
        streamUploadRequest(req, false);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kIndexHtml, strlen(kIndexHtml));
    return ESP_OK;
}

esp_err_t systemInfoHandler(httpd_req_t *req) {
    JsonDocument doc;
    doc["VERSION"] = "Pixel-CYD";
    JsonObject sd = doc["SD"].to<JsonObject>();
    if (isSdCardMounted() || setupSdCard()) {
        uint64_t total = SD.totalBytes();
        uint64_t used = SD.usedBytes();
        uint64_t freeB = total >= used ? (total - used) : 0;
        sd["free"] = humanReadableSize(freeB);
        sd["used"] = humanReadableSize(used);
        sd["total"] = humanReadableSize(total);
        sd["freeBytes"] = static_cast<double>(freeB);
        sd["usedBytes"] = static_cast<double>(used);
        sd["totalBytes"] = static_cast<double>(total);
    }

    JsonArray apps = doc["APPS"].to<JsonArray>();
    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        JsonObject o = apps.add<JsonObject>();
        o["label"] = app.label;
        o["name"] = app.name.isEmpty() ? app.label : app.name;
    }

    String json;
    serializeJson(doc, json);
    sendText(req, "application/json", json);
    return ESP_OK;
}

esp_err_t otaHandler(httpd_req_t *req) {
    WebParamMap params = readParams(req);
    if (params.has("command") && params.has("size")) {
        s_otaCommand = params.get("command").toInt();
        s_otaFileSize = static_cast<size_t>(params.get("size").toInt());
        if (s_otaFileSize > 0) {
            String err;
            if (!prepareWebInstallContext(s_otaCommand, s_otaFileSize, params, err)) {
                clearWebInstallContext();
                setStatus(LauncherWebUiState::Error, err.c_str(), 0, 0, 0, true);
                sendText(req, 400, "text/plain", err.isEmpty() ? "Install prep failed" : err);
                return ESP_OK;
            }
            s_otaArmed = true;
            setStatus(LauncherWebUiState::Flashing, "Receiving firmware...", 0, 0, s_otaFileSize);
            sendText(req, "text/plain", "OK");
            return ESP_OK;
        }
    }
    sendText(req, 400, "text/plain", "Invalid OTA request");
    return ESP_OK;
}

esp_err_t otaFileHandler(httpd_req_t *req) {
    bool flash = s_otaArmed && s_installCtx.active;
    s_otaArmed = false;
    streamUploadRequest(req, flash);
    return ESP_OK;
}

esp_err_t updateFromSdHandler(httpd_req_t *req) {
    WebParamMap params = readParams(req);
    String path = params.get("fileName");
    if (path.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing fileName");
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_statusMux);
    strncpy(s_pendingSdInstall, path.c_str(), sizeof(s_pendingSdInstall) - 1);
    s_pendingSdInstall[sizeof(s_pendingSdInstall) - 1] = '\0';
    s_status.eventSeq++;
    taskEXIT_CRITICAL(&s_statusMux);
    setStatus(LauncherWebUiState::Flashing, "Queued SD install...", 0, 0, 0, true);
    sendText(req, "text/plain", "Starting Update");
    return ESP_OK;
}

esp_err_t listFilesHandler(httpd_req_t *req) {
    String folder = queryValue(req, "folder");
    if (folder.isEmpty()) folder = "/";
    s_uploadFolder = folder;

    if (!isSdCardMounted() && !setupSdCard()) {
        sendText(req, "text/plain", "pa:" + folder + ":0\n");
        return ESP_OK;
    }
    if (!SD.exists(folder)) {
        sendText(req, "text/plain", "pa:" + folder + ":0\n");
        return ESP_OK;
    }

    std::vector<LauncherSdEntry> entries;
    listSdDirectory(folder, entries, false, true);
    String out = "pa:" + folder + ":0\n";
    out.reserve(1024);
    for (const LauncherSdEntry &e : entries) {
        if (e.isDir) {
            out += "Fo:" + e.name + ":0\n";
        } else {
            out += "Fi:" + e.name + ":" + humanReadableSize(e.size) + "\n";
        }
    }
    sendText(req, "text/plain", out);
    return ESP_OK;
}

esp_err_t fileHandler(httpd_req_t *req) {
    String name = queryValue(req, "name");
    String action = queryValue(req, "action");
    if (name.isEmpty() || action.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing name or action");
        return ESP_OK;
    }
    if (!isSdCardMounted() && !setupSdCard()) {
        sendText(req, 500, "text/plain", "SD card not mounted");
        return ESP_OK;
    }
    if (action == "create") {
        bool ok = SD.mkdir(name);
        sendText(req, ok ? 200 : 500, "text/plain", ok ? "OK" : "Create failed");
        return ESP_OK;
    }
    if (!SD.exists(name)) {
        sendText(req, 404, "text/plain", "Not found");
        return ESP_OK;
    }
    if (action == "delete") {
        bool ok = deleteFromSd(name);
        sendText(req, ok ? 200 : 500, "text/plain", ok ? "Deleted" : "Delete failed");
        return ESP_OK;
    }
    if (action == "download") {
        File f = SD.open(name);
        if (!f) {
            sendText(req, 404, "text/plain", "Open failed");
            return ESP_OK;
        }
        httpd_resp_set_type(req, "application/octet-stream");
        String disp = "attachment; filename=\"" + name.substring(name.lastIndexOf('/') + 1) + "\"";
        httpd_resp_set_hdr(req, "Content-Disposition", disp.c_str());
        uint8_t *buff = acquireScratch(kUploadChunkFallback);
        if (!buff) {
            f.close();
            sendText(req, 500, "text/plain", "OOM");
            return ESP_OK;
        }
        while (f.available()) {
            size_t n = f.read(buff, kUploadChunkFallback);
            if (httpd_resp_send_chunk(req, reinterpret_cast<const char *>(buff), n) != ESP_OK) break;
        }
        httpd_resp_send_chunk(req, nullptr, 0);
        f.close();
        return ESP_OK;
    }
    sendText(req, 400, "text/plain", "Unknown action");
    return ESP_OK;
}

esp_err_t bootAppHandler(httpd_req_t *req) {
    WebParamMap params = readParams(req);
    String label = params.get("label");
    if (label.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing label");
        return ESP_OK;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        sendText(req, 400, "text/plain", error.isEmpty() ? "Table read failed" : error);
        return ESP_OK;
    }
    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
    if (!entry || !entry->isOtaApp()) {
        sendText(req, 404, "text/plain", "App not found");
        return ESP_OK;
    }
    if (!launcherPartitionSetOtaBoot(table, entry->subtype, &error)) {
        sendText(req, 500, "text/plain", error.isEmpty() ? "Boot set failed" : error);
        return ESP_OK;
    }
    launcherBleBondsSwitchTo(label.c_str());
    g_launcherLastInstalledApp = launcherAppDisplayNameForLabel(label.c_str());
    launcherUiSaveState();

    taskENTER_CRITICAL(&s_statusMux);
    s_pendingReboot = true;
    taskEXIT_CRITICAL(&s_statusMux);
    setStatus(LauncherWebUiState::Success, "Launching App...", 100, 0, 0, true);
    sendText(req, "text/plain", "OK");
    return ESP_OK;
}

esp_err_t rebootHandler(httpd_req_t *req) {
    taskENTER_CRITICAL(&s_statusMux);
    s_pendingReboot = true;
    taskEXIT_CRITICAL(&s_statusMux);
    sendText(req, "text/plain", "Rebooting");
    return ESP_OK;
}

esp_err_t fallbackHandler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

void registerRoute(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    httpd_register_uri_handler(s_server, &route);
}

} // namespace

bool launcherWebUiStart(bool apMode, String *errorOut) {
    if (s_server) return true;

    String ipStr;
    String ssidStr;
    if (apMode) {
        WiFi.mode(WIFI_AP);
        if (!WiFi.softAP(kApSsid)) {
            if (errorOut) *errorOut = "Could not start Wi-Fi hotspot.";
            return false;
        }
        launcherDelayMs(150);
        ipStr = WiFi.softAPIP().toString();
        ssidStr = kApSsid;
        s_startedAp = true;
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            if (errorOut) *errorOut = "Connect to Wi-Fi first, or use Hotspot mode.";
            return false;
        }
        ipStr = WiFi.localIP().toString();
        ssidStr = WiFi.SSID();
        s_startedAp = false;
    }

    s_server = launcherWebServerStart(kWebPort);
    if (!s_server) {
        if (s_startedAp) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            s_startedAp = false;
        }
        if (errorOut) *errorOut = "Could not start HTTP server on port 80.";
        return false;
    }

    registerRoute("/ping", HTTP_GET, pingHandler);
    registerRoute("/", HTTP_GET, rootHandler);
    registerRoute("/", HTTP_POST, rootHandler);
    registerRoute("/systeminfo", HTTP_GET, systemInfoHandler);
    registerRoute("/OTA", HTTP_POST, otaHandler);
    registerRoute("/OTAFILE", HTTP_POST, otaFileHandler);
    registerRoute("/UPDATE", HTTP_POST, updateFromSdHandler);
    registerRoute("/listfiles", HTTP_GET, listFilesHandler);
    registerRoute("/file", HTTP_GET, fileHandler);
    registerRoute("/bootapp", HTTP_GET, bootAppHandler);
    registerRoute("/bootapp", HTTP_POST, bootAppHandler);
    registerRoute("/reboot", HTTP_GET, rebootHandler);
    registerRoute("/*", HTTP_GET, fallbackHandler);

    String fullUrl = String("http://") + ipStr;

    taskENTER_CRITICAL(&s_statusMux);
    s_status = LauncherWebUiStatus();
    s_status.running = true;
    s_status.apMode = apMode;
    s_status.state = LauncherWebUiState::Idle;
    strncpy(s_status.url, fullUrl.c_str(), sizeof(s_status.url) - 1);
    strncpy(s_status.ssid, ssidStr.c_str(), sizeof(s_status.ssid) - 1);
    strncpy(s_status.statusText, "Waiting for browser...", sizeof(s_status.statusText) - 1);
    s_pendingReboot = false;
    s_pendingAppsSync = false;
    s_pendingSyncLabel[0] = '\0';
    s_pendingSdInstall[0] = '\0';
    taskEXIT_CRITICAL(&s_statusMux);

    launcherConsolePrintf("WebUI started (%s): %s\n", apMode ? "AP" : "STA", fullUrl.c_str());
    return true;
}

void launcherWebUiStop() {
    if (s_server) {
        launcherWebServerStop(s_server);
        s_server = nullptr;
    }
    clearWebInstallContext();
    s_otaArmed = false;
    releaseScratch();

    if (s_startedAp) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        s_startedAp = false;
    }

    taskENTER_CRITICAL(&s_statusMux);
    s_status.running = false;
    s_status.apMode = false;
    s_status.state = LauncherWebUiState::Stopped;
    s_status.progressPct = 0;
    s_status.bytesDone = 0;
    s_status.bytesTotal = 0;
    strncpy(s_status.statusText, "Server stopped", sizeof(s_status.statusText) - 1);
    s_pendingReboot = false;
    s_pendingAppsSync = false;
    s_pendingSdInstall[0] = '\0';
    taskEXIT_CRITICAL(&s_statusMux);
}

bool launcherWebUiIsRunning() {
    return s_server != nullptr;
}

LauncherWebUiStatus launcherWebUiGetStatus() {
    LauncherWebUiStatus copy;
    taskENTER_CRITICAL(&s_statusMux);
    copy = s_status;
    taskEXIT_CRITICAL(&s_statusMux);
    return copy;
}

bool launcherWebUiPoll() {
    bool doReboot = false;
    bool doSync = false;
    char syncLabel[16] = {};
    char sdInstallPath[96] = {};

    taskENTER_CRITICAL(&s_statusMux);
    if (s_pendingAppsSync) {
        doSync = true;
        s_pendingAppsSync = false;
        strncpy(syncLabel, s_pendingSyncLabel, sizeof(syncLabel) - 1);
        s_pendingSyncLabel[0] = '\0';
    }
    if (s_pendingSdInstall[0]) {
        strncpy(sdInstallPath, s_pendingSdInstall, sizeof(sdInstallPath) - 1);
        s_pendingSdInstall[0] = '\0';
    }
    if (s_pendingReboot) {
        doReboot = true;
        s_pendingReboot = false;
    }
    taskEXIT_CRITICAL(&s_statusMux);

    if (doSync) {
        launcherUiNotifyAppsChanged(syncLabel[0] ? syncLabel : nullptr);
    }
    if (sdInstallPath[0]) {
        bool ok = updateFromSD(String(sdInstallPath), true, false);
        setStatus(ok ? LauncherWebUiState::Success : LauncherWebUiState::Error,
                  ok ? "Installed from SD!" : "SD install failed",
                  ok ? 100 : 0, 0, 0, true);
    }
    if (doReboot) {
        launcherWebUiStop();
        launcherUiReboot();
    }
    return doSync || sdInstallPath[0] != '\0';
}

#endif // !LAUNCHER_WEBUI_HOST_TEST
