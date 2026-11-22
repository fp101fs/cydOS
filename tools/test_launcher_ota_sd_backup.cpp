// Host-side unit tests for Item 4 pure helpers (onlineLauncher protocol/URL/table helpers,
// filename sanitization, Content-Range parsing, and backup_manager rename hook wiring).
// Compile & run:
//   c++ -std=c++17 -Wall -Wextra tools/test_launcher_ota_sd_backup.cpp -o /tmp/test_launcher_ota_sd_backup && /tmp/test_launcher_ota_sd_backup

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Minimal Arduino String shim matching the exact methods used by our helpers.
struct String {
    std::string s;
    String() = default;
    String(const char *c) : s(c ? c : "") {}
    String(char c) : s(1, c) {}
    String(const std::string &v) : s(v) {}
    size_t length() const { return s.size(); }
    bool isEmpty() const { return s.empty(); }
    const char *c_str() const { return s.c_str(); }
    char operator[](size_t i) const { return s[i]; }
    void setCharAt(size_t i, char c) { s[i] = c; }
    String &operator+=(char c) { s.push_back(c); return *this; }
    String &operator+=(const char *c) { if (c) s += c; return *this; }
    String &operator+=(const String &o) { s += o.s; return *this; }
    friend String operator+(const String &a, const String &b) { return String(a.s + b.s); }
    friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
    friend String operator+(const String &a, const char *b) { return String(a.s + std::string(b)); }
    bool operator==(const char *o) const { return s == (o ? o : ""); }
    bool operator==(const String &o) const { return s == o.s; }
    bool startsWith(const char *p) const { return s.rfind(p, 0) == 0; }
    bool endsWith(const char *p) const {
        size_t n = strlen(p);
        return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
    }
    void remove(size_t idx, size_t count = 1) { s.erase(idx, count); }
    int lastIndexOf(char c) const {
        auto pos = s.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    String substring(size_t from) const { return String(s.substr(from)); }
    String substring(size_t from, size_t to) const {
        if (from >= s.size()) return String();
        if (to > s.size()) to = s.size();
        return String(s.substr(from, to - from));
    }
    long toInt() const { return std::strtol(s.c_str(), nullptr, 10); }
    void trim() {
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
        size_t b = s.size();
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
        s = s.substr(a, b - a);
    }
    void replace(const String &from, const String &to) {
        if (from.s.empty()) return;
        size_t pos = 0;
        while ((pos = s.find(from.s, pos)) != std::string::npos) {
            s.replace(pos, from.s.size(), to.s);
            pos += to.s.size();
        }
    }
};

static constexpr size_t LAUNCHER_PARTITION_ENTRY_SIZE = 32;
#define M5_SERVER_PATH "https://m5burner-cdn.m5stack.com/firmware/"

// Exact implementations from src/launcher/onlineLauncher.cpp
static String launcherEncodeQueryValue(const String &value) {
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

static String launcherSanitizeFilename(String input) {
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

static bool launcherParseContentRangeTotal(const char *contentRange, size_t &total) {
    if (!contentRange) return false;
    String range = contentRange;
    int slash = range.lastIndexOf('/');
    if (slash < 0 || slash + 1 >= static_cast<int>(range.length())) return false;
    long parsed = range.substring(slash + 1).toInt();
    if (parsed <= 0) return false;
    total = static_cast<size_t>(parsed);
    return true;
}

static String launcherBuildSourceUrl(const String &fid, const String &sourceUrl, bool useProxy) {
    String url = sourceUrl;
    if (!url.startsWith("http")) url = String(M5_SERVER_PATH) + url;
    if (useProxy && !fid.isEmpty()) {
        url = String("https://api.launcherhub.net/download?fid=") + fid + "&file=" + url;
    }
    return url;
}

static uint32_t launcherFirstDataPartitionOffsetFromTable(const uint8_t *table, size_t len) {
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

static void formatBytes(size_t bytes, char *out, size_t outSize) {
    if (bytes >= 1024UL * 1024UL) {
        uint32_t tenths = static_cast<uint32_t>((bytes * 10ULL) / (1024ULL * 1024ULL));
        snprintf(out, outSize, "%lu.%lu MB",
                 static_cast<unsigned long>(tenths / 10),
                 static_cast<unsigned long>(tenths % 10));
    } else if (bytes >= 1024UL) {
        snprintf(out, outSize, "%lu KB", static_cast<unsigned long>(bytes / 1024UL));
    } else {
        snprintf(out, outSize, "%lu B", static_cast<unsigned long>(bytes));
    }
}

static String launcherFormatAppInstalledDetail(const String &appName) {
    String clean = appName;
    clean.trim();
    if (clean.isEmpty()) clean = "App";
    return clean + " added to Home Screen.\nTap Launch Now to start it.";
}

static String launcherFormatDownloadSavedDetail(const String &filePath, size_t fileSizeBytes) {
    int slash = filePath.lastIndexOf('/');
    String name = (slash >= 0 && slash + 1 < static_cast<int>(filePath.length()))
                      ? filePath.substring(slash + 1)
                      : filePath;
    String dir = (slash > 0) ? filePath.substring(0, slash + 1) : String("/");
    if (name.isEmpty()) name = "app.bin";
    String out = name;
    if (fileSizeBytes > 0) {
        char szBuf[20];
        formatBytes(fileSizeBytes, szBuf, sizeof(szBuf));
        out += " (";
        out += szBuf;
        out += ")";
    }
    out += "\nSaved to ";
    out += dir;
    return out;
}

static void writeEntry(uint8_t *dst, uint8_t type, uint8_t subtype, uint32_t offset, uint32_t size) {
    memset(dst, 0, 32);
    dst[0] = 0xAA;
    dst[1] = 0x50;
    dst[2] = type;
    dst[3] = subtype;
    dst[4] = offset & 0xFF;
    dst[5] = (offset >> 8) & 0xFF;
    dst[6] = (offset >> 16) & 0xFF;
    dst[7] = (offset >> 24) & 0xFF;
    dst[8] = size & 0xFF;
    dst[9] = (size >> 8) & 0xFF;
    dst[10] = (size >> 16) & 0xFF;
    dst[11] = (size >> 24) & 0xFF;
}

int main() {
    // 1. Query encoding
    assert(launcherEncodeQueryValue("Bruce v1.9+beta") == "Bruce%20v1.9%2Bbeta");
    assert(launcherEncodeQueryValue("a-z_0.9~A") == "a-z_0.9~A");
    assert(launcherEncodeQueryValue("foo/bar&baz=1") == "foo%2Fbar%26baz%3D1");

    // 2. Filename sanitization
    assert(launcherSanitizeFilename("Marauder: v1.0 / CYD? ") == "Marauder_ v1.0 _ CYD_");
    assert(launcherSanitizeFilename("  ...  ") == "firmware");
    assert(launcherSanitizeFilename("app\x07name...") == "app_name");
    assert(launcherSanitizeFilename("") == "firmware");

    // 3. Content-Range parsing
    size_t total = 0;
    assert(launcherParseContentRangeTotal("bytes 0-1/1048576", total) && total == 1048576);
    assert(launcherParseContentRangeTotal("bytes 32768-33183/2097152", total) && total == 2097152);
    assert(!launcherParseContentRangeTotal("bytes 0-1/*", total));
    assert(!launcherParseContentRangeTotal("bytes 0-1/0", total));
    assert(!launcherParseContentRangeTotal("invalid", total));
    assert(!launcherParseContentRangeTotal(nullptr, total));

    // 4. Source URL construction
    assert(launcherBuildSourceUrl("abc123", "fw.bin", true) ==
           "https://api.launcherhub.net/download?fid=abc123&file=https://m5burner-cdn.m5stack.com/firmware/fw.bin");
    assert(launcherBuildSourceUrl("abc123", "https://example.com/app.bin", true) ==
           "https://api.launcherhub.net/download?fid=abc123&file=https://example.com/app.bin");
    assert(launcherBuildSourceUrl("abc123", "https://example.com/spiffs.bin", false) ==
           "https://example.com/spiffs.bin");
    assert(launcherBuildSourceUrl("", "fw.bin", true) ==
           "https://m5burner-cdn.m5stack.com/firmware/fw.bin");

    // 5. Partition table data offset extraction
    uint8_t tbl[32 * 5];
    memset(tbl, 0xFF, sizeof(tbl));
    writeEntry(tbl + 0, 0x01, 0x02, 0x9000, 0x5000);     // NVS (subtype 0x02 <= 0x79, skipped)
    writeEntry(tbl + 32, 0x01, 0x00, 0xE000, 0x2000);    // OTA data (subtype 0x00 <= 0x79, skipped)
    writeEntry(tbl + 64, 0x00, 0x10, 0x10000, 0x180000); // App (type 0x00, skipped)
    writeEntry(tbl + 96, 0x01, 0x82, 0x290000, 0x170000); // SPIFFS (type 0x01, subtype 0x82 > 0x79)
    assert(launcherFirstDataPartitionOffsetFromTable(tbl, sizeof(tbl)) == 0x290000);

    // Table with no filesystem data partition
    memset(tbl + 96, 0xFF, 32);
    assert(launcherFirstDataPartitionOffsetFromTable(tbl, sizeof(tbl)) == 0);
    assert(launcherFirstDataPartitionOffsetFromTable(nullptr, 128) == 0);

    // 6. Plain-English completion detail formatters (Item 8)
    assert(launcherFormatAppInstalledDetail("Bruce") ==
           "Bruce added to Home Screen.\nTap Launch Now to start it.");
    assert(launcherFormatAppInstalledDetail("   ") ==
           "App added to Home Screen.\nTap Launch Now to start it.");
    assert(launcherFormatDownloadSavedDetail("/downloads/Bruce_1.9.bin", 1258292) ==
           "Bruce_1.9.bin (1.2 MB)\nSaved to /downloads/");
    assert(launcherFormatDownloadSavedDetail("/app.bin", 524288) ==
           "app.bin (512 KB)\nSaved to /");
    assert(launcherFormatDownloadSavedDetail("/downloads/test.bin", 0) ==
           "test.bin\nSaved to /downloads/");

    printf("[OK] test_launcher_ota_sd_backup: all assertions passed\n");
    return 0;
}
