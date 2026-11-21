#include "notes_store.h"
#include "../config/hw_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static constexpr const char* NOTES_NVS_NS   = "notes";
static constexpr uint32_t    SD_SPI_FREQ_HZ = 4000000;
static constexpr size_t      SD_BUF_MAX     = NOTES_MAX_COUNT * (NOTES_TITLE_MAX + NOTES_BODY_MAX + 40) + 64;

static NoteItem s_notes[NOTES_MAX_COUNT] = {};
static uint8_t  s_count  = 0;
static bool     s_inited = false;

static void makeSlotKey(char prefix, uint8_t idx, char outKey[4]) {
    outKey[0] = prefix;
    outKey[1] = (char)('0' + idx);
    outKey[2] = '\0';
}

static void copyTrimmed(const char* src, char* dst, size_t cap, bool singleLine) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src && (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')) {
        ++src;
    }

    size_t len = 0;
    while (src[len] && len + 1 < cap) {
        char c = src[len];
        if (singleLine && (c == '\r' || c == '\n')) {
            break;
        }
        dst[len++] = c;
    }
    dst[len] = '\0';

    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t' ||
                       dst[len - 1] == '\r' || dst[len - 1] == '\n')) {
        dst[--len] = '\0';
    }
}

void notesStoreDeriveTitle(const char* title, const char* body, char* outTitle, size_t outCap) {
    if (!outTitle || outCap == 0) return;
    copyTrimmed(title, outTitle, outCap, true);
    if (outTitle[0] != '\0') return;

    copyTrimmed(body, outTitle, outCap, true);
    if (outTitle[0] != '\0') return;

    strncpy(outTitle, "Untitled", outCap - 1);
    outTitle[outCap - 1] = '\0';
}

static void persistSlot(Preferences& prefs, uint8_t idx) {
    char tk[4], bk[4];
    makeSlotKey('t', idx, tk);
    makeSlotKey('b', idx, bk);
    prefs.putString(tk, s_notes[idx].title);
    prefs.putString(bk, s_notes[idx].body);
}

static void persistAllToNvs() {
    Preferences prefs;
    if (!prefs.begin(NOTES_NVS_NS, false)) return;

    prefs.putBool("init", true);
    prefs.putUChar("cnt", s_count);
    for (uint8_t i = 0; i < s_count; ++i) {
        persistSlot(prefs, i);
    }
    for (uint8_t i = s_count; i < NOTES_MAX_COUNT; ++i) {
        char tk[4], bk[4];
        makeSlotKey('t', i, tk);
        makeSlotKey('b', i, bk);
        prefs.remove(tk);
        prefs.remove(bk);
    }
    prefs.end();
}

void notesStoreInit() {
    memset(s_notes, 0, sizeof(s_notes));
    s_count = 0;

    Preferences prefs;
    bool hasInit = false;
    if (prefs.begin(NOTES_NVS_NS, true)) {
        hasInit = prefs.getBool("init", false);
        if (hasInit) {
            uint8_t cnt = prefs.getUChar("cnt", 0);
            if (cnt > NOTES_MAX_COUNT) cnt = NOTES_MAX_COUNT;
            for (uint8_t i = 0; i < cnt; ++i) {
                char tk[4], bk[4];
                makeSlotKey('t', i, tk);
                makeSlotKey('b', i, bk);
                String t = prefs.getString(tk, "");
                String b = prefs.getString(bk, "");
                notesStoreDeriveTitle(t.c_str(), b.c_str(), s_notes[i].title, NOTES_TITLE_MAX);
                strncpy(s_notes[i].body, b.c_str(), NOTES_BODY_MAX - 1);
                s_notes[i].body[NOTES_BODY_MAX - 1] = '\0';
            }
            s_count = cnt;
        }
        prefs.end();
    }

    if (!hasInit) {
        s_count = 1;
        strncpy(s_notes[0].title, "Welcome to Notes", NOTES_TITLE_MAX - 1);
        strncpy(s_notes[0].body,
                "Saved in ESP32 NVS flash.\nTap to edit or + New to add.\nInsert microSD to export/import.",
                NOTES_BODY_MAX - 1);
        persistAllToNvs();
    }

    s_inited = true;
    Serial.printf("[NOTES] Loaded %u note(s) from NVS\n", (unsigned)s_count);
}

static void ensureInit() {
    if (!s_inited) {
        notesStoreInit();
    }
}

uint8_t notesStoreCount() {
    ensureInit();
    return s_count;
}

const NoteItem* notesStoreGet(uint8_t index) {
    ensureInit();
    if (index >= s_count) return nullptr;
    return &s_notes[index];
}

int8_t notesStoreSave(int8_t index, const char* title, const char* body) {
    ensureInit();

    int8_t target = index;
    bool isNew = (index < 0);
    if (isNew) {
        if (s_count >= NOTES_MAX_COUNT) return -1;
        target = (int8_t)s_count;
    } else if ((uint8_t)index >= s_count) {
        return -1;
    }

    char cleanTitle[NOTES_TITLE_MAX];
    notesStoreDeriveTitle(title, body, cleanTitle, sizeof(cleanTitle));

    strncpy(s_notes[target].title, cleanTitle, NOTES_TITLE_MAX - 1);
    s_notes[target].title[NOTES_TITLE_MAX - 1] = '\0';

    if (body) {
        strncpy(s_notes[target].body, body, NOTES_BODY_MAX - 1);
        s_notes[target].body[NOTES_BODY_MAX - 1] = '\0';
    } else {
        s_notes[target].body[0] = '\0';
    }

    if (isNew) {
        ++s_count;
    }

    Preferences prefs;
    if (prefs.begin(NOTES_NVS_NS, false)) {
        prefs.putBool("init", true);
        prefs.putUChar("cnt", s_count);
        persistSlot(prefs, (uint8_t)target);
        prefs.end();
    }

    return target;
}

bool notesStoreDelete(uint8_t index) {
    ensureInit();
    if (index >= s_count) return false;

    for (uint8_t i = index; i + 1 < s_count; ++i) {
        s_notes[i] = s_notes[i + 1];
    }
    --s_count;
    memset(&s_notes[s_count], 0, sizeof(NoteItem));

    persistAllToNvs();
    return true;
}

size_t notesStoreSerialize(char* outBuf, size_t outCap) {
    ensureInit();
    if (!outBuf || outCap == 0) return 0;
    size_t pos = 0;
    outBuf[0] = '\0';

    for (uint8_t i = 0; i < s_count; ++i) {
        int n = snprintf(outBuf + pos, outCap - pos,
                         "=== NOTE: %s ===\n%s\n=== END ===\n",
                         s_notes[i].title, s_notes[i].body);
        if (n < 0) break;
        if ((size_t)n >= outCap - pos) {
            pos = outCap - 1;
            outBuf[pos] = '\0';
            break;
        }
        pos += (size_t)n;
    }
    return pos;
}

static int8_t findByTitle(const char* title) {
    for (uint8_t i = 0; i < s_count; ++i) {
        if (strcmp(s_notes[i].title, title) == 0) {
            return (int8_t)i;
        }
    }
    return -1;
}

static bool mergeOne(const char* title, const char* body) {
    char cleanTitle[NOTES_TITLE_MAX];
    notesStoreDeriveTitle(title, body, cleanTitle, sizeof(cleanTitle));

    int8_t existing = findByTitle(cleanTitle);
    if (existing >= 0) {
        strncpy(s_notes[existing].body, body ? body : "", NOTES_BODY_MAX - 1);
        s_notes[existing].body[NOTES_BODY_MAX - 1] = '\0';
        return true;
    }
    if (s_count >= NOTES_MAX_COUNT) {
        return false;
    }

    strncpy(s_notes[s_count].title, cleanTitle, NOTES_TITLE_MAX - 1);
    s_notes[s_count].title[NOTES_TITLE_MAX - 1] = '\0';
    strncpy(s_notes[s_count].body, body ? body : "", NOTES_BODY_MAX - 1);
    s_notes[s_count].body[NOTES_BODY_MAX - 1] = '\0';
    ++s_count;
    return true;
}

uint8_t notesStoreParseAndMerge(const char* text, size_t len) {
    ensureInit();
    if (!text || len == 0) return 0;

    static constexpr const char* HDR_PREFIX = "=== NOTE: ";
    static constexpr size_t HDR_PREFIX_LEN  = 10;
    static constexpr const char* HDR_SUFFIX = " ===";
    static constexpr size_t HDR_SUFFIX_LEN  = 4;
    static constexpr const char* END_MARKER = "=== END ===";

    uint8_t imported = 0;
    bool foundStructured = false;
    bool inNote = false;
    char curTitle[NOTES_TITLE_MAX] = "";
    char curBody[NOTES_BODY_MAX]   = "";
    size_t curBodyLen = 0;

    size_t pos = 0;
    while (pos < len) {
        size_t lineStart = pos;
        while (pos < len && text[pos] != '\n') {
            ++pos;
        }
        size_t lineEnd = pos;
        if (pos < len && text[pos] == '\n') {
            ++pos;
        }
        if (lineEnd > lineStart && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        size_t lineLen = lineEnd - lineStart;
        const char* line = text + lineStart;

        if (!inNote) {
            if (lineLen >= HDR_PREFIX_LEN + HDR_SUFFIX_LEN &&
                memcmp(line, HDR_PREFIX, HDR_PREFIX_LEN) == 0 &&
                memcmp(line + lineLen - HDR_SUFFIX_LEN, HDR_SUFFIX, HDR_SUFFIX_LEN) == 0) {
                foundStructured = true;
                inNote = true;
                size_t tLen = lineLen - HDR_PREFIX_LEN - HDR_SUFFIX_LEN;
                if (tLen >= NOTES_TITLE_MAX) tLen = NOTES_TITLE_MAX - 1;
                memcpy(curTitle, line + HDR_PREFIX_LEN, tLen);
                curTitle[tLen] = '\0';
                curBody[0] = '\0';
                curBodyLen = 0;
            }
        } else {
            if (lineLen == strlen(END_MARKER) && memcmp(line, END_MARKER, lineLen) == 0) {
                if (mergeOne(curTitle, curBody)) {
                    ++imported;
                }
                inNote = false;
            } else {
                if (curBodyLen > 0 && curBodyLen + 1 < NOTES_BODY_MAX) {
                    curBody[curBodyLen++] = '\n';
                }
                size_t copyLen = lineLen;
                if (curBodyLen + copyLen >= NOTES_BODY_MAX) {
                    copyLen = (NOTES_BODY_MAX - 1) - curBodyLen;
                }
                if (copyLen > 0) {
                    memcpy(curBody + curBodyLen, line, copyLen);
                    curBodyLen += copyLen;
                }
                curBody[curBodyLen] = '\0';
            }
        }
    }

    // Handle trailing structured note without closing === END ===
    if (inNote) {
        if (mergeOne(curTitle, curBody)) {
            ++imported;
        }
    }

    // Plain-text fallback: import whole non-empty file as a single note
    if (!foundStructured) {
        char plainBody[NOTES_BODY_MAX];
        size_t copyLen = len < (NOTES_BODY_MAX - 1) ? len : (NOTES_BODY_MAX - 1);
        memcpy(plainBody, text, copyLen);
        plainBody[copyLen] = '\0';

        // Strip trailing CR/LF/spaces
        while (copyLen > 0 && (plainBody[copyLen - 1] == '\r' || plainBody[copyLen - 1] == '\n' ||
                               plainBody[copyLen - 1] == ' ' || plainBody[copyLen - 1] == '\t')) {
            plainBody[--copyLen] = '\0';
        }
        if (copyLen > 0) {
            if (mergeOne("", plainBody)) {
                ++imported;
            }
        }
    }

    if (imported > 0) {
        persistAllToNvs();
    }
    return imported;
}

static bool mountSdCard(SPIClass& sdSpi) {
    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, sdSpi, SD_SPI_FREQ_HZ)) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return false;
    }
    return true;
}

NotesSdResult notesStoreExportSd(uint8_t* outExported) {
    ensureInit();
    if (outExported) *outExported = 0;

    if (s_count == 0) {
        return NOTES_SD_EMPTY;
    }

    SPIClass sdSpi(VSPI);
    if (!mountSdCard(sdSpi)) {
        return NOTES_SD_NO_CARD;
    }

    File f = SD.open(NOTES_SD_PATH, FILE_WRITE);
    if (!f) {
        SD.end();
        return NOTES_SD_IO_ERROR;
    }

    char* buf = new char[SD_BUF_MAX];
    if (!buf) {
        f.close();
        SD.end();
        return NOTES_SD_IO_ERROR;
    }

    size_t len = notesStoreSerialize(buf, SD_BUF_MAX);
    size_t written = (len > 0) ? f.write((const uint8_t*)buf, len) : 0;
    delete[] buf;
    f.close();
    SD.end();

    if (written != len) {
        return NOTES_SD_IO_ERROR;
    }

    if (outExported) *outExported = s_count;
    Serial.printf("[NOTES] Exported %u note(s) to SD:%s (%u B)\n",
                  (unsigned)s_count, NOTES_SD_PATH, (unsigned)written);
    return NOTES_SD_OK;
}

NotesSdResult notesStoreImportSd(uint8_t* outImported) {
    ensureInit();
    if (outImported) *outImported = 0;

    SPIClass sdSpi(VSPI);
    if (!mountSdCard(sdSpi)) {
        return NOTES_SD_NO_CARD;
    }

    if (!SD.exists(NOTES_SD_PATH)) {
        SD.end();
        return NOTES_SD_NO_FILE;
    }

    File f = SD.open(NOTES_SD_PATH, FILE_READ);
    if (!f) {
        SD.end();
        return NOTES_SD_IO_ERROR;
    }

    char* buf = new char[SD_BUF_MAX];
    if (!buf) {
        f.close();
        SD.end();
        return NOTES_SD_IO_ERROR;
    }

    size_t readLen = f.readBytes(buf, SD_BUF_MAX - 1);
    buf[readLen] = '\0';
    f.close();
    SD.end();

    if (readLen == 0) {
        delete[] buf;
        return NOTES_SD_EMPTY;
    }

    uint8_t imported = notesStoreParseAndMerge(buf, readLen);
    delete[] buf;

    if (outImported) *outImported = imported;
    Serial.printf("[NOTES] Imported %u note(s) from SD:%s\n",
                  (unsigned)imported, NOTES_SD_PATH);
    return (imported > 0) ? NOTES_SD_OK : NOTES_SD_EMPTY;
}
