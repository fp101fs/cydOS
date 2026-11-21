#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

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

static lv_obj_t* s_body = nullptr;
static int8_t s_openFolder = -1;

static void renderFiles();

static void folderClickCb(lv_event_t* e) {
    m3PulseFeedback();
    s_openFolder = (int8_t)(intptr_t)lv_event_get_user_data(e);
    renderFiles();
}

static void backRootCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_openFolder = -1;
    renderFiles();
}

static void fileClickCb(lv_event_t* e) {
    const char* fname = (const char*)lv_event_get_user_data(e);
    m3PulseFeedback();
    m3ShowToast(fname, 1200);
}

static void renderFiles() {
    lv_obj_clean(s_body);

    if (s_openFolder < 0) {
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
            lv_obj_set_style_text_color(name, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_align(name, LV_ALIGN_TOP_LEFT, 26, -2);

            lv_obj_t* sub = lv_label_create(card);
            lv_label_set_text(sub, s_folders[i].info);
            lv_obj_set_style_text_color(sub, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
            lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 26, 2);
        }
    } else {
        lv_obj_t* backChip = m3PillBtn(s_body, LV_SYMBOL_LEFT "  Internal Storage", M3Color::SURFACE_HIGHEST, M3Color::AMBER, LV_PCT(100), 32);
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
            lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 24, 0);
        }
    }
}

void buildAppFiles(lv_obj_t* body) {
    s_body = body;
    s_openFolder = -1;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    renderFiles();
}
