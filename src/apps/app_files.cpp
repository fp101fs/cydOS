#include "app_registry.h"
#include "../config/hw_config.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

struct MockFolder {
    const char* name;
    const char* info;
    const char* files[3];
};

static const MockFolder s_folders[5] = {
    { "Pictures",  "12 items • 18.4 MB",  { "IMG_20261121.jpg", "Pixel_Wall.png", "Sunset_04.jpg" } },
    { "Music",     "8 items • 42.1 MB",   { "Midnight_Drive.mp3", "Neon_Rain.flac", "LoFi_Mix.mp3" } },
    { "Documents", "5 items • 2.3 MB",    { "Notes_Backup.txt", "CYD_Pinout.pdf", "Todo.md" } },
    { "Downloads", "3 items • 9.8 MB",    { "pixel_cyd_v1.bin", "lvgl_theme.json", "wallpaper.png" } },
    { "System",    "Read-only • 4.0 MB",  { "build.prop", "lv_conf.h", "touch.cal" } }
};

struct SdEntry {
    char     name[44];
    uint32_t size;
    bool     isDir;
};

static constexpr size_t   SD_MAX_ENTRIES       = 24;
static constexpr uint32_t FILES_SD_SPI_FREQ_HZ = 4000000;

static lv_obj_t* s_body       = nullptr;
static int8_t    s_openFolder = -1;     // -1 = root, 0..4 = mock folder
static bool      s_inSdView   = false;
static bool      s_sdMounted  = false;
static uint32_t  s_sdSizeMb   = 0;
static char      s_sdPath[96] = "/";
static SdEntry   s_sdEntries[SD_MAX_ENTRIES];
static size_t    s_sdCount    = 0;

static void renderFiles();

static void copyStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static void formatSize(uint32_t bytes, char* out, size_t cap) {
    if (bytes >= 1048576UL) {
        snprintf(out, cap, "%.1f MB", (float)bytes / 1048576.0f);
    } else if (bytes >= 1024UL) {
        snprintf(out, cap, "%.1f KB", (float)bytes / 1024.0f);
    } else {
        snprintf(out, cap, "%lu B", (unsigned long)bytes);
    }
}

static bool scanSdDirectory(const char* path) {
    s_sdCount = 0;
    s_sdMounted = false;
    s_sdSizeMb = 0;

    SPIClass sdSpi(VSPI);
    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, sdSpi, FILES_SD_SPI_FREQ_HZ)) {
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return false;
    }

    s_sdMounted = true;
    s_sdSizeMb = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        SD.end();
        return true;
    }

    File entry = dir.openNextFile();
    while (entry && s_sdCount < SD_MAX_ENTRIES) {
        const char* rawName = entry.name();
        const char* base = rawName;
        if (rawName) {
            const char* slash = strrchr(rawName, '/');
            if (slash && slash[1] != '\0') base = slash + 1;
        }
        if (base && base[0] && base[0] != '.') {
            SdEntry& item = s_sdEntries[s_sdCount++];
            copyStr(item.name, sizeof(item.name), base);
            item.isDir = entry.isDirectory();
            item.size = item.isDir ? 0 : (uint32_t)entry.size();
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    SD.end();

    // Sort directories before files
    for (size_t i = 1; i < s_sdCount; ++i) {
        SdEntry key = s_sdEntries[i];
        size_t j = i;
        while (j > 0) {
            const SdEntry& prev = s_sdEntries[j - 1];
            bool keyFirst = (key.isDir != prev.isDir)
                ? key.isDir
                : (strcasecmp(key.name, prev.name) < 0);
            if (!keyFirst) break;
            s_sdEntries[j] = s_sdEntries[j - 1];
            --j;
        }
        s_sdEntries[j] = key;
    }
    return true;
}

static void folderClickCb(lv_event_t* e) {
    m3PulseFeedback();
    s_inSdView = false;
    s_openFolder = (int8_t)(intptr_t)lv_event_get_user_data(e);
    renderFiles();
}

static void backRootCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_inSdView = false;
    s_openFolder = -1;
    renderFiles();
}

static void openSdBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_inSdView = true;
    s_openFolder = -1;
    copyStr(s_sdPath, sizeof(s_sdPath), "/");
    if (!scanSdDirectory(s_sdPath)) {
        m3ShowToast("No Micro SD card detected", 1400);
    }
    renderFiles();
}

static void sdBackCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (strcmp(s_sdPath, "/") == 0) {
        s_inSdView = false;
        renderFiles();
        return;
    }
    char* lastSlash = strrchr(s_sdPath, '/');
    if (lastSlash && lastSlash != s_sdPath) {
        *lastSlash = '\0';
    } else {
        copyStr(s_sdPath, sizeof(s_sdPath), "/");
    }
    scanSdDirectory(s_sdPath);
    renderFiles();
}

static void sdEntryClickCb(lv_event_t* e) {
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_sdCount) return;
    m3PulseFeedback();

    const SdEntry& item = s_sdEntries[idx];
    if (item.isDir) {
        char nextPath[96];
        if (strcmp(s_sdPath, "/") == 0) {
            snprintf(nextPath, sizeof(nextPath), "/%s", item.name);
        } else {
            snprintf(nextPath, sizeof(nextPath), "%s/%s", s_sdPath, item.name);
        }
        copyStr(s_sdPath, sizeof(s_sdPath), nextPath);
        scanSdDirectory(s_sdPath);
        renderFiles();
    } else {
        char sz[20];
        formatSize(item.size, sz, sizeof(sz));
        char msg[72];
        snprintf(msg, sizeof(msg), "%s\n%s", item.name, sz);
        m3ShowToast(msg, 1600);
    }
}

static void fileClickCb(lv_event_t* e) {
    const char* fname = (const char*)lv_event_get_user_data(e);
    m3PulseFeedback();
    m3ShowToast(fname, 1200);
}

static void renderSdView() {
    char backLbl[48];
    if (strcmp(s_sdPath, "/") == 0) {
        if (s_sdMounted && s_sdSizeMb > 0) {
            if (s_sdSizeMb >= 1024) {
                snprintf(backLbl, sizeof(backLbl), LV_SYMBOL_LEFT "  SD (%.1f GB)", (float)s_sdSizeMb / 1024.0f);
            } else {
                snprintf(backLbl, sizeof(backLbl), LV_SYMBOL_LEFT "  SD (%lu MB)", (unsigned long)s_sdSizeMb);
            }
        } else {
            snprintf(backLbl, sizeof(backLbl), LV_SYMBOL_LEFT "  Files");
        }
    } else {
        snprintf(backLbl, sizeof(backLbl), LV_SYMBOL_LEFT "  %s", s_sdPath);
    }

    lv_obj_t* backChip = m3PillBtn(s_body, backLbl, m3SurfaceHighestHex(), M3Color::TERTIARY, LV_PCT(100), 30);
    lv_obj_add_event_cb(backChip, sdBackCb, LV_EVENT_CLICKED, nullptr);

    if (!s_sdMounted) {
        lv_obj_t* card = m3Card(s_body, LV_PCT(100), 86, 14);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 6, 0);

        lv_obj_t* msg = lv_label_create(card);
        lv_label_set_text(msg, "No Micro SD card detected");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);

        lv_obj_t* retry = m3PillBtn(card, LV_SYMBOL_REFRESH "  Retry", M3Color::TERTIARY_CONT, M3Color::TERTIARY, 96, 26);
        lv_obj_add_event_cb(retry, openSdBtnCb, LV_EVENT_CLICKED, nullptr);
        return;
    }

    if (s_sdCount == 0) {
        lv_obj_t* msg = lv_label_create(s_body);
        lv_label_set_text(msg, "Folder is empty");
        lv_obj_set_style_text_color(msg, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        return;
    }

    for (size_t i = 0; i < s_sdCount; ++i) {
        const SdEntry& item = s_sdEntries[i];
        lv_obj_t* card = m3Card(s_body, LV_PCT(100), 40, 12);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 4, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, sdEntryClickCb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        lv_obj_t* ico = lv_label_create(card);
        lv_label_set_text(ico, item.isDir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(ico, lv_color_hex(item.isDir ? M3Color::AMBER : M3Color::TERTIARY), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_14, 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, item.name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_width(lbl, item.isDir ? 168 : 124);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 22, 0);

        if (!item.isDir) {
            char sz[20];
            formatSize(item.size, sz, sizeof(sz));
            lv_obj_t* szLbl = lv_label_create(card);
            lv_label_set_text(szLbl, sz);
            lv_obj_set_style_text_color(szLbl, lv_color_hex(m3TextSecondaryHex()), 0);
            lv_obj_set_style_text_font(szLbl, &lv_font_montserrat_10, 0);
            lv_obj_align(szLbl, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
}

static void renderFiles() {
    if (!s_body) return;
    lv_obj_clean(s_body);

    if (s_inSdView) {
        renderSdView();
        return;
    }

    if (s_openFolder < 0) {
        // Simple Micro SD Card button at top of Files
        lv_obj_t* sdBtn = m3PillBtn(s_body, LV_SYMBOL_SD_CARD "  Micro SD Card",
                                    M3Color::TERTIARY_CONT, M3Color::TERTIARY, LV_PCT(100), 34);
        lv_obj_add_event_cb(sdBtn, openSdBtnCb, LV_EVENT_CLICKED, nullptr);

        for (int i = 0; i < 5; ++i) {
            lv_obj_t* card = m3Card(s_body, LV_PCT(100), 46, 14);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, folderClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

            lv_obj_t* ico = lv_label_create(card);
            lv_label_set_text(ico, LV_SYMBOL_DIRECTORY);
            lv_obj_set_style_text_color(ico, lv_color_hex(M3Color::AMBER), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_16, 0);
            lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t* name = lv_label_create(card);
            lv_label_set_text(name, s_folders[i].name);
            lv_obj_set_style_text_color(name, lv_color_hex(m3TextPrimaryHex()), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_align(name, LV_ALIGN_TOP_LEFT, 26, -2);

            lv_obj_t* sub = lv_label_create(card);
            lv_label_set_text(sub, s_folders[i].info);
            lv_obj_set_style_text_color(sub, lv_color_hex(m3TextSecondaryHex()), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
            lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 26, 2);
        }
    } else {
        lv_obj_t* backChip = m3PillBtn(s_body, LV_SYMBOL_LEFT "  Internal Storage", m3SurfaceHighestHex(), M3Color::AMBER, LV_PCT(100), 32);
        lv_obj_add_event_cb(backChip, backRootCb, LV_EVENT_CLICKED, nullptr);

        const MockFolder& f = s_folders[s_openFolder];
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* card = m3Card(s_body, LV_PCT(100), 42, 12);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, fileClickCb, LV_EVENT_CLICKED, (void*)f.files[i]);

            lv_obj_t* ico = lv_label_create(card);
            lv_label_set_text(ico, LV_SYMBOL_FILE);
            lv_obj_set_style_text_color(ico, lv_color_hex(M3Color::PRIMARY), 0);
            lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, f.files[i]);
            lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 24, 0);
        }
    }
}

static void filesDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
}

static void refreshAppFilesTheme() {
    if (!s_body) return;
    renderFiles();
}

void buildAppFiles(lv_obj_t* body) {
    s_body = body;
    s_openFolder = -1;
    s_inSdView = false;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, filesDeleteCb, LV_EVENT_DELETE, nullptr);
    renderFiles();
}

REGISTER_APP_THEMED(files, "files", "Files", LV_SYMBOL_DIRECTORY, 0xFDD663, 0xE5A00D, buildAppFiles, nullptr, refreshAppFilesTheme, 10);
