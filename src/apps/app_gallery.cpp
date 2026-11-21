#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

struct PhotoMock {
    const char* title;
    const char* meta;
    uint32_t bgHex;
    uint32_t accentHex;
};

static const PhotoMock s_photos[6] = {
    { "Alpine Dawn",    "Nov 21 • 4032x3024", 0x1D3557, 0x8AB4F8 },
    { "Moss Forest",    "Nov 19 • 4032x3024", 0x1B4332, 0x81C995 },
    { "Dusk Horizon",   "Nov 17 • 4032x3024", 0x3A2F52, 0xC5B4E3 },
    { "Coastal Mist",   "Nov 14 • 4032x3024", 0x173F49, 0x78D9EC },
    { "City Lights",    "Nov 10 • 4032x3024", 0x4A2226, 0xF28B82 },
    { "Golden Hour",    "Nov 05 • 4032x3024", 0x453917, 0xFDD663 }
};

static lv_obj_t* s_body = nullptr;
static int8_t s_selected = -1;

static void renderGallery();

static void thumbClickCb(lv_event_t* e) {
    m3PulseFeedback();
    s_selected = (int8_t)(intptr_t)lv_event_get_user_data(e);
    renderGallery();
}

static void backGridCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_selected = -1;
    renderGallery();
}

static void renderGallery() {
    lv_obj_clean(s_body);

    if (s_selected >= 0) {
        lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_body, 8, 0);

        const PhotoMock& p = s_photos[s_selected];
        lv_obj_t* preview = m3Card(s_body, LV_PCT(100), 144, 16);
        lv_obj_set_style_bg_color(preview, lv_color_hex(p.bgHex), 0);
        lv_obj_set_style_border_color(preview, lv_color_hex(p.accentHex), 0);

        lv_obj_t* ico = lv_label_create(preview);
        lv_label_set_text(ico, LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_color(ico, lv_color_hex(p.accentHex), 0);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_36, 0);
        lv_obj_center(ico);

        lv_obj_t* tLbl = lv_label_create(s_body);
        lv_label_set_text(tLbl, p.title);
        lv_obj_set_style_text_color(tLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_16, 0);

        lv_obj_t* mLbl = lv_label_create(s_body);
        lv_label_set_text(mLbl, p.meta);
        lv_obj_set_style_text_color(mLbl, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
        lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_12, 0);

        lv_obj_t* backBtn = m3PillBtn(s_body, LV_SYMBOL_LEFT "  All Photos", M3Color::SURFACE_HIGH, M3Color::TEXT_PRIMARY, 120, 32);
        lv_obj_add_event_cb(backBtn, backGridCb, LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(s_body, 6, 0);
        lv_obj_set_style_pad_column(s_body, 6, 0);

        for (int i = 0; i < 6; ++i) {
            lv_obj_t* tile = m3Card(s_body, 68, 86, 12);
            lv_obj_set_style_bg_color(tile, lv_color_hex(s_photos[i].bgHex), 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(s_photos[i].accentHex), 0);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(tile, thumbClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

            lv_obj_t* ico = lv_label_create(tile);
            lv_label_set_text(ico, LV_SYMBOL_IMAGE);
            lv_obj_set_style_text_color(ico, lv_color_hex(s_photos[i].accentHex), 0);
            lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
            lv_obj_align(ico, LV_ALIGN_CENTER, 0, -6);

            lv_obj_t* lbl = lv_label_create(tile);
            lv_label_set_text(lbl, s_photos[i].title);
            lv_obj_set_style_text_color(lbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_width(lbl, 56);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 2);
        }
    }
}

void buildAppGallery(lv_obj_t* body) {
    s_body = body;
    s_selected = -1;
    renderGallery();
}

REGISTER_APP(gallery, "gallery", "Gallery", LV_SYMBOL_IMAGE, 0x81C995, 0x22A06B, buildAppGallery, nullptr, 20);
