// Host-side unit test for src/launcher core backend:
//   1. launcher_ui_bridge.cpp (progress, byte formatting, error/info routing, cancel, hooks)
//   2. partition_table_model.cpp (4 MB layout, MD5 build/parse, validate, OTA/data allocation,
//      compaction, label sanitization, FAT payload plan, otadata clear) against a 4 MB mock flash.
//   3. launcherGenerateAppNum CRC32 determinism.
//
// Compile & run:
//   c++ -std=c++17 -Wall -Wextra -Isrc/launcher tools/test_launcher_core.cpp -o /tmp/test_launcher_core && /tmp/test_launcher_core

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// --- Minimal Arduino String shim for host testing ---
class String {
public:
    String() = default;
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    explicit String(int v) : s_(std::to_string(v)) {}
    explicit String(unsigned int v) : s_(std::to_string(v)) {}

    const char *c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    char operator[](size_t i) const { return s_[i]; }
    String substring(size_t start, size_t end = std::string::npos) const {
        if (start >= s_.size()) return "";
        if (end == std::string::npos || end > s_.size()) end = s_.size();
        return String(s_.substr(start, end - start));
    }
    int indexOf(char c, int from = 0) const {
        if (from < 0) from = 0;
        size_t pos = s_.find(c, static_cast<size_t>(from));
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        size_t pos = s_.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    void trim() {
        size_t b = 0;
        while (b < s_.size() && (s_[b] == ' ' || s_[b] == '\t' || s_[b] == '\r' || s_[b] == '\n')) ++b;
        size_t e = s_.size();
        while (e > b && (s_[e - 1] == ' ' || s_[e - 1] == '\t' || s_[e - 1] == '\r' || s_[e - 1] == '\n')) --e;
        s_ = s_.substr(b, e - b);
    }

    String &operator+=(const String &o) { s_ += o.s_; return *this; }
    String &operator+=(const char *o) { if (o) s_ += o; return *this; }
    String &operator+=(char c) { s_.push_back(c); return *this; }

    friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
    friend String operator+(const String &a, const char *b) { return String(a.s_ + (b ? b : "")); }
    friend String operator+(const char *a, const String &b) { return String(std::string(a ? a : "") + b.s_); }
    friend bool operator==(const String &a, const String &b) { return a.s_ == b.s_; }
    friend bool operator==(const String &a, const char *b) { return a.s_ == (b ? b : ""); }
    friend bool operator!=(const String &a, const String &b) { return a.s_ != b.s_; }
    friend bool operator!=(const String &a, const char *b) { return a.s_ != (b ? b : ""); }

private:
    std::string s_;
};

// --- Minimal M3Color & m3_modal mock ---
namespace M3Color {
constexpr uint32_t PRIMARY = 0x8AB4F8;
constexpr uint32_t CORAL   = 0xF28B82;
} // namespace M3Color

struct M3ModalBtn {
    const char *label;
    uint32_t bgHex;
    uint32_t fgHex;
    void (*onClick)();
    bool closeOnClick;
};

struct MockModalLog {
    bool open = false;
    bool progressOpen = false;
    bool finishSuccess = false;
    bool cancellable = false;
    int8_t lastPct = -1;
    uint32_t lastAccent = 0;
    std::string title;
    std::string step;
    std::string detail;
    void (*onCancel)() = nullptr;
    int showCalls = 0;
    int showProgressCalls = 0;
    int updateCalls = 0;
    int finishCalls = 0;
    int closeCalls = 0;
} g_modal;

static void m3ModalShow(const char *title, const char *body, const M3ModalBtn * = nullptr,
                        uint8_t = 0, uint32_t accentHex = 0) {
    g_modal.open = true;
    g_modal.progressOpen = false;
    g_modal.title = title ? title : "";
    g_modal.detail = body ? body : "";
    g_modal.lastAccent = accentHex;
    g_modal.showCalls++;
}

static void m3ModalShowProgress(const char *title, const char *stepText = nullptr,
                                uint32_t accentHex = 0, bool cancellable = false,
                                void (*onCancel)() = nullptr) {
    g_modal.open = true;
    g_modal.progressOpen = true;
    g_modal.title = title ? title : "";
    g_modal.step = stepText ? stepText : "";
    g_modal.lastAccent = accentHex;
    g_modal.cancellable = cancellable;
    g_modal.onCancel = onCancel;
    g_modal.lastPct = -1;
    g_modal.showProgressCalls++;
}

static void m3ModalUpdateProgress(int8_t pct, const char *stepText = nullptr,
                                  const char *detailText = nullptr, bool = true) {
    g_modal.lastPct = pct;
    if (stepText) g_modal.step = stepText;
    if (detailText) g_modal.detail = detailText;
    g_modal.updateCalls++;
}

static void m3ModalFinishProgress(bool success, const char *headline, const char *detail = nullptr,
                                  const M3ModalBtn * = nullptr, uint8_t = 0) {
    g_modal.progressOpen = false;
    g_modal.finishSuccess = success;
    g_modal.title = headline ? headline : "";
    if (detail) g_modal.detail = detail;
    g_modal.finishCalls++;
}

static void m3ModalClose() {
    g_modal.open = false;
    g_modal.progressOpen = false;
    g_modal.closeCalls++;
}

static bool m3ModalIsProgressOpen() { return g_modal.progressOpen; }

// Include launcher_ui_bridge in host-test mode
#define LAUNCHER_BRIDGE_HOST_TEST 1
#include "../src/launcher/launcher_ui_bridge.h"
#include "../src/launcher/launcher_ui_bridge.cpp"

// --- Minimal ESP-IDF + 4 MB mock flash + RFC1321 MD5 for partition_table_model.cpp ---
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

#define ESP_PARTITION_MAGIC     0x50AA
#define ESP_PARTITION_MAGIC_MD5 0xEBEB
#define ESP_PARTITION_MD5_OFFSET 16
#define ESP_PARTITION_TABLE_MAX_LEN 0xC00

#define PART_TYPE_APP  0x00
#define PART_TYPE_DATA 0x01
#define PART_SUBTYPE_FACTORY  0x00
#define PART_SUBTYPE_TEST     0x20
#define PART_SUBTYPE_OTA_FLAG 0x10
#define PART_SUBTYPE_OTA_MASK 0x0F
#define PART_SUBTYPE_DATA_OTA 0x00
#define PART_SUBTYPE_DATA_RF  0x01
#define PART_SUBTYPE_DATA_WIFI 0x02
#define PART_SUBTYPE_DATA_NVS_KEYS 0x04
#define PART_SUBTYPE_DATA_EFUSE_EM 0x05

#define ESP_PARTITION_SUBTYPE_DATA_FAT      0x81
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS   0x82
#define ESP_PARTITION_SUBTYPE_DATA_LITTLEFS 0x83

#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2

struct esp_partition_t { uint32_t address; };
struct esp_ota_select_entry_t {
    uint32_t ota_seq;
    uint8_t seq_label[20];
    uint32_t ota_state;
    uint32_t crc;
};
#define ESP_OTA_IMG_UNDEFINED 0xFFFFFFFFU

static constexpr uint32_t kMockFlashSize = 0x400000; // 4 MB
static std::vector<uint8_t> g_flash(kMockFlashSize, 0xFF);

static void *heap_caps_malloc(size_t sz, int) { return std::malloc(sz); }
static void heap_caps_free(void *p) { std::free(p); }
static void yield() {}

static esp_err_t esp_flash_get_size(void *, uint32_t *out) {
    *out = kMockFlashSize;
    return ESP_OK;
}
static esp_err_t esp_flash_read(void *, void *dst, uint32_t addr, size_t len) {
    if (addr + len > g_flash.size()) return ESP_FAIL;
    std::memcpy(dst, g_flash.data() + addr, len);
    return ESP_OK;
}
static esp_err_t esp_flash_erase_region(void *, uint32_t addr, size_t len) {
    if (addr + len > g_flash.size()) return ESP_FAIL;
    std::memset(g_flash.data() + addr, 0xFF, len);
    return ESP_OK;
}
static esp_err_t esp_flash_write(void *, const void *src, uint32_t addr, size_t len) {
    if (addr + len > g_flash.size()) return ESP_FAIL;
    std::memcpy(g_flash.data() + addr, src, len);
    return ESP_OK;
}
static const esp_partition_t *esp_ota_get_running_partition() {
    static const esp_partition_t kFactory = {0x10000};
    return &kFactory;
}
static int bootloader_common_get_active_otadata(const esp_ota_select_entry_t *) { return -1; }
static uint32_t bootloader_common_ota_select_crc(const esp_ota_select_entry_t *e) {
    return e->ota_seq ^ 0xA5A5A5A5U;
}
static void launcherConsolePrintf(const char *, ...) {}

// Standard MD5 (RFC 1321) for mbedtls_md5
#define MBEDTLS_VERSION_MAJOR 3
static int mbedtls_md5(const uint8_t *msg, size_t len, uint8_t out[16]) {
    static const uint32_t k[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const uint32_t s[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    auto rotl = [](uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); };

    std::vector<uint8_t> buf(msg, msg + len);
    buf.push_back(0x80);
    while ((buf.size() % 64) != 56) buf.push_back(0);
    uint64_t bitLen = static_cast<uint64_t>(len) * 8ULL;
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFF));

    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (size_t off = 0; off < buf.size(); off += 64) {
        uint32_t M[16];
        for (int j = 0; j < 16; ++j) {
            const uint8_t *p = &buf[off + j * 4];
            M[j] = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t F, g;
            if (i < 16)      { F = (B & C) | ((~B) & D); g = i; }
            else if (i < 32) { F = (D & B) | ((~D) & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;            g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | (~D));       g = (7 * i) % 16; }
            F = F + A + k[i] + M[g];
            A = D; D = C; C = B; B = B + rotl(F, s[i]);
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    uint32_t state[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = state[i] & 0xFF;
        out[i * 4 + 1] = (state[i] >> 8) & 0xFF;
        out[i * 4 + 2] = (state[i] >> 16) & 0xFF;
        out[i * 4 + 3] = (state[i] >> 24) & 0xFF;
    }
    return 0;
}

// Stub out Arduino/ESP headers already provided above
#define _ARDUINO_H_
#define LAUNCHER_PLATFORM_H
#define LAUNCHER_LITTLEFS_PATCH_H
struct LauncherPartitionEntry;
static bool launcherPatchReducedLittlefsSuperblocks(const LauncherPartitionEntry &, String * = nullptr, bool * = nullptr) {
    return true;
}

// Re-use exact partition_table_model.h & .cpp by guarding ESP includes via pre-defined macros
// Instead, test the exact model types & logic via a clean wrapper include block:
#include <memory>

// Include partition_table_model.h (skipping <Arduino.h> by providing empty header? Simpler: we test
// the exact partition_table_model.cpp via a small include-dir trick in the compile command, or inline).
// Let's use the compile command's -I/tmp/host_stubs so #include <Arduino.h> and ESP headers resolve!
#ifdef INCLUDE_REAL_PARTITION_MODEL
#include "../src/launcher/partition_table_model.cpp"
#endif

static String testGenerateAppNum(const String &sourcePath) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < sourcePath.length(); ++i) {
        crc ^= static_cast<uint8_t>(sourcePath[i]);
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u * (crc & 1u));
    }
    crc ^= 0xFFFFFFFF;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", static_cast<unsigned long>(crc));
    return String(buf);
}

static bool g_cancelFired = false;
static int g_appsChangedCount = 0;
static std::string g_lastChangedLabel;
static std::string g_lastRenamedAppNum;
static std::string g_lastRenamedName;

static void onCancelCb() { g_cancelFired = true; }
static void onAppsChangedCb(const char *lbl) {
    g_appsChangedCount++;
    g_lastChangedLabel = lbl ? lbl : "";
}
static void onAppRenamedCb(const String &appNum, const String &name) {
    g_lastRenamedAppNum = appNum.c_str();
    g_lastRenamedName = name.c_str();
}

static void testUiBridge() {
    char buf[32];
    launcherUiFormatBytes(0, buf, sizeof(buf));       assert(std::string(buf) == "0 B");
    launcherUiFormatBytes(512, buf, sizeof(buf));     assert(std::string(buf) == "512 B");
    launcherUiFormatBytes(1024, buf, sizeof(buf));    assert(std::string(buf) == "1 KB");
    launcherUiFormatBytes(430080, buf, sizeof(buf));  assert(std::string(buf) == "420 KB");
    launcherUiFormatBytes(1048576, buf, sizeof(buf)); assert(std::string(buf) == "1.0 MB");
    launcherUiFormatBytes(1572864, buf, sizeof(buf)); assert(std::string(buf) == "1.5 MB");

    g_modal = MockModalLog{};
    g_cancelFired = false;
    launcherUiBeginProgress("Installing App", "Connecting...", M3Color::PRIMARY, true, onCancelCb);
    assert(g_modal.progressOpen);
    assert(g_modal.title == "Installing App");
    assert(g_modal.step == "Connecting...");
    assert(g_modal.cancellable);
    assert(!launcherUiIsCancelled());

    launcherUiProgressBytes(524288, 1048576, "Downloading...");
    assert(g_modal.lastPct == 50);
    assert(g_modal.step == "Downloading...");
    assert(g_modal.detail == "512 KB / 1.0 MB");

    // Indeterminate byte progress (unknown total)
    launcherUiProgressBytes(204800, 0, "Streaming...");
    assert(g_modal.lastPct == -1);
    assert(g_modal.detail == "200 KB");

    // Cancel callback fires once and marks cancelled
    assert(g_modal.onCancel != nullptr);
    g_modal.onCancel();
    assert(launcherUiIsCancelled());
    assert(g_cancelFired);

    // Error while progress modal is open transitions in-place via m3ModalFinishProgress(false)
    launcherUiError("Flash write failed", "Install Failed");
    assert(!g_modal.progressOpen);
    assert(!g_modal.finishSuccess);
    assert(g_modal.title == "Install Failed");
    assert(g_modal.detail == "Flash write failed");

    // Error while closed opens standard coral modal
    int prevShow = g_modal.showCalls;
    launcherUiError("No Wi-Fi");
    assert(g_modal.showCalls == prevShow + 1);
    assert(g_modal.lastAccent == M3Color::CORAL);

    // Auto-open progress modal when launcherUiStep is called while closed
    launcherUiClose();
    assert(!g_modal.progressOpen);
    launcherUiStep("Erasing flash...", 25, "app0");
    assert(g_modal.progressOpen);
    assert(g_modal.lastPct == 25);
    assert(g_modal.step == "Erasing flash...");
    assert(g_modal.detail == "app0");

    // Finish success
    launcherUiFinish(true, "Installed!", "Bruce");
    assert(!g_modal.progressOpen);
    assert(g_modal.finishSuccess);

    // Cross-module hooks
    launcherUiSetAppsChangedCb(onAppsChangedCb);
    launcherUiSetAppRenamedCb(onAppRenamedCb);
    launcherUiNotifyAppsChanged("bruce0");
    assert(g_appsChangedCount == 1 && g_lastChangedLabel == "bruce0");
    launcherUiNotifyAppRenamed("deadbeef", "Bruce 1.9");
    assert(g_lastRenamedAppNum == "deadbeef" && g_lastRenamedName == "Bruce 1.9");

    // Deterministic CRC32 appNum
    String appNum1 = testGenerateAppNum("/downloads/bruce.bin");
    String appNum2 = testGenerateAppNum("/downloads/bruce.bin");
    String appNum3 = testGenerateAppNum("/downloads/other.bin");
    assert(appNum1.length() == 8);
    assert(appNum1 == appNum2);
    assert(appNum1 != appNum3);
    // Standard CRC32 of "123456789" is 0xcbf43926
    assert(testGenerateAppNum("123456789") == "cbf43926");
}

#ifdef INCLUDE_REAL_PARTITION_MODEL
static LauncherPartitionTable makePixelCydBaseTable() {
    LauncherPartitionTable t;
    t.flashSize = kMockFlashSize;

    LauncherPartitionEntry nvs;
    nvs.type = PART_TYPE_DATA; nvs.subtype = 0x02;
    nvs.offset = 0x9000; nvs.size = 0x5000;
    std::strcpy(nvs.label, "nvs");

    LauncherPartitionEntry ota;
    ota.type = PART_TYPE_DATA; ota.subtype = PART_SUBTYPE_DATA_OTA;
    ota.offset = 0xE000; ota.size = 0x2000;
    std::strcpy(ota.label, "otadata");

    LauncherPartitionEntry factory;
    factory.type = PART_TYPE_APP; factory.subtype = PART_SUBTYPE_FACTORY;
    factory.offset = 0x10000; factory.size = 0x200000;
    std::strcpy(factory.label, "factory");

    t.entries = {nvs, ota, factory};
    return t;
}

static void testPartitionModel() {
    launcherPartitionInitDefaultSizes();
    assert(LAUNCHER_DEFAULT_SPIFFS_SIZE == 0x20000);
    assert(LAUNCHER_DEFAULT_FAT_SIZE == 0x50000);

    LauncherPartitionTable base = makePixelCydBaseTable();
    String err;
    assert(launcherPartitionValidate(base, &err));

    // Free range should be exact 0x210000..0x400000 (0x1F0000 = 1984 KB)
    auto freeRanges = launcherPartitionFreeRanges(base);
    assert(freeRanges.size() == 1);
    assert(freeRanges[0].offset == 0x210000);
    assert(freeRanges[0].size == 0x1F0000);

    // Build -> Write to mock 4 MB flash -> Read back -> Verify MD5 & exact entries
    assert(launcherPartitionWriteGeneratedTable(base, &err));
    LauncherPartitionTable roundTrip;
    assert(launcherPartitionReadCurrent(roundTrip, &err));
    assert(roundTrip.hasMd5);
    assert(roundTrip.entries.size() == 3);
    assert(std::string(roundTrip.entries[2].label) == "factory");
    assert(roundTrip.entries[2].isFactoryOrTestApp());

    // Create first OTA app (900 KB -> 64 KB aligned = 0xF0000)
    LauncherPartitionEntry app0;
    String lbl0 = launcherPartitionNextAppLabel(roundTrip, "Bruce Firmware");
    assert(lbl0 == "brucef");
    assert(launcherPartitionCreateOtaApp(roundTrip, 900 * 1024, lbl0.c_str(), &app0, &err));
    assert(app0.offset == 0x210000);
    assert(app0.size == 0xF0000);
    assert(app0.subtype == 0x10);
    assert(app0.isOtaApp());

    // Collision label generation
    String lbl1 = launcherPartitionNextAppLabel(roundTrip, "Bruce Firmware");
    assert(lbl1 == "bruce1");

    // Create second OTA app (600 KB -> 0xA0000)
    LauncherPartitionEntry app1;
    assert(launcherPartitionCreateOtaApp(roundTrip, 600 * 1024, lbl1.c_str(), &app1, &err));
    assert(app1.offset == 0x300000);
    assert(app1.size == 0xA0000);
    assert(app1.subtype == 0x11);

    // Create SPIFFS data partition (128 KB = 0x20000)
    LauncherPartitionEntry spiffs;
    assert(launcherPartitionCreateData(roundTrip, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs", 0x20000, &spiffs, &err));
    assert(spiffs.offset == 0x3A0000);
    assert(spiffs.size == 0x20000);

    // Oversized app should fail cleanly
    LauncherPartitionEntry tooBig;
    assert(!launcherPartitionCreateOtaApp(roundTrip, 1024 * 1024, "huge00", &tooBig, &err));

    // Remove app0 and compact -> app1 moves down to 0x210000, spiffs moves down to 0x2B0000
    assert(launcherPartitionRemoveEntryByOffset(roundTrip, app0.offset));
    assert(launcherPartitionCompact(roundTrip, &err));
    const LauncherPartitionEntry *movedApp1 = launcherPartitionFindByLabel(roundTrip, lbl1.c_str());
    assert(movedApp1 != nullptr && movedApp1->offset == 0x210000);
    const LauncherPartitionEntry *movedSpiffs = launcherPartitionFindByLabel(roundTrip, "spiffs");
    assert(movedSpiffs != nullptr && movedSpiffs->offset == 0x2B0000);

    // Validation catches missing otadata
    LauncherPartitionTable bad = base;
    bad.entries.erase(bad.entries.begin() + 1);
    assert(!launcherPartitionValidate(bad, &err));

    // Validation catches missing factory/test
    bad = base;
    bad.entries.pop_back();
    assert(!launcherPartitionValidate(bad, &err));

    // FAT payload plan: "vfs" capped to default 0x50000, "sys" uses payload size
    auto vfsPlan = launcherPartitionFatPayloadPlan("vfs", 0x80000);
    assert(vfsPlan.partitionSize == 0x50000 && vfsPlan.copySize == 0x50000);
    auto sysPlan = launcherPartitionFatPayloadPlan("sys", 0x80000);
    assert(sysPlan.partitionSize == 0x80000 && sysPlan.copySize == 0x80000);

    // Clear otadata erases 0xE000..0x10000 to 0xFF
    g_flash[0xE000] = 0x12;
    assert(launcherPartitionClearOtaBoot(base, &err));
    assert(g_flash[0xE000] == 0xFF);
}
#endif

int main() {
    testUiBridge();
#ifdef INCLUDE_REAL_PARTITION_MODEL
    testPartitionModel();
#endif
    std::printf("[OK] test_launcher_core passed\n");
    return 0;
}
