#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <stdio.h>

struct Track {
    const char* title;
    const char* artist;
    uint32_t artBg;
    uint32_t artAccent;
    uint16_t durationSec;
};

static const Track s_tracks[3] = {
    { "Midnight Drive",   "Synthwave Collective", 0x261B38, 0xC5B4E3, 214 },
    { "Material Dreams",  "Pixel Soundscapes",    0x162D3B, 0x78D9EC, 186 },
    { "Neon Rain",        "Lo-Fi Chillhop",       0x361E24, 0xF28B82, 162 }
};

static uint8_t s_trackIdx = 0;
static bool s_playing = true;
static uint16_t s_posSec = 64;
static uint32_t s_lastTickMs = 0;

static lv_obj_t* s_artBox = nullptr;
static lv_obj_t* s_titleLbl = nullptr;
static lv_obj_t* s_artistLbl = nullptr;
static lv_obj_t* s_progBar = nullptr;
static lv_obj_t* s_timeLbl = nullptr;
static lv_obj_t* s_playLbl = nullptr;

static void refreshMusicUi() {
    if (!s_titleLbl) return;
    const Track& t = s_tracks[s_trackIdx];
    lv_obj_set_style_bg_color(s_artBox, lv_color_hex(t.artBg), 0);
    lv_obj_set_style_border_color(s_artBox, lv_color_hex(t.artAccent), 0);
    lv_label_set_text(s_titleLbl, t.title);
    lv_label_set_text(s_artistLbl, t.artist);
    lv_bar_set_range(s_progBar, 0, t.durationSec);
    lv_bar_set_value(s_progBar, s_posSec, LV_ANIM_OFF);
    lv_label_set_text(s_playLbl, s_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    char buf[24];
    snprintf(buf, sizeof(buf), "%u:%02u / %u:%02u",
             s_posSec / 60, s_posSec % 60,
             t.durationSec / 60, t.durationSec % 60);
    lv_label_set_text(s_timeLbl, buf);
}

static void prevCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_trackIdx = (s_trackIdx + 2) % 3;
    s_posSec = 0;
    refreshMusicUi();
}

static void nextCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_trackIdx = (s_trackIdx + 1) % 3;
    s_posSec = 0;
    refreshMusicUi();
}

static void playCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    s_playing = !s_playing;
    refreshMusicUi();
}

void buildAppMusic(lv_obj_t* body) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    // Album art card
    s_artBox = m3Card(body, 112, 92, 18);
    lv_obj_align(s_artBox, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t* noteIco = lv_label_create(s_artBox);
    lv_label_set_text(noteIco, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(noteIco, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(noteIco, &lv_font_montserrat_36, 0);
    lv_obj_center(noteIco);

    s_titleLbl = lv_label_create(body);
    lv_obj_set_style_text_color(s_titleLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_titleLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_titleLbl, LV_ALIGN_TOP_MID, 0, 102);

    s_artistLbl = lv_label_create(body);
    lv_obj_set_style_text_color(s_artistLbl, lv_color_hex(M3Color::TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(s_artistLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_artistLbl, LV_ALIGN_TOP_MID, 0, 124);

    s_progBar = lv_bar_create(body);
    lv_obj_set_size(s_progBar, 200, 6);
    lv_obj_align(s_progBar, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_set_style_bg_color(s_progBar, lv_color_hex(M3Color::SURFACE_HIGHEST), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progBar, lv_color_hex(0xF28B82), LV_PART_INDICATOR);

    s_timeLbl = lv_label_create(body);
    lv_obj_set_style_text_color(s_timeLbl, lv_color_hex(M3Color::TEXT_MUTED), 0);
    lv_obj_set_style_text_font(s_timeLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_timeLbl, LV_ALIGN_TOP_MID, 0, 158);

    // Controls row
    lv_obj_t* prevBtn = m3PillBtn(body, LV_SYMBOL_PREV, M3Color::SURFACE_HIGH, M3Color::TEXT_PRIMARY, 44, 38);
    lv_obj_align(prevBtn, LV_ALIGN_BOTTOM_MID, -62, -2);
    lv_obj_add_event_cb(prevBtn, prevCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* playBtn = lv_btn_create(body);
    lv_obj_set_size(playBtn, 56, 44);
    lv_obj_align(playBtn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(playBtn, lv_color_hex(0xF28B82), 0);
    lv_obj_set_style_radius(playBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(playBtn, 0, 0);
    lv_obj_add_event_cb(playBtn, playCb, LV_EVENT_CLICKED, nullptr);
    s_playLbl = lv_label_create(playBtn);
    lv_obj_set_style_text_color(s_playLbl, lv_color_hex(0x2B0C0E), 0);
    lv_obj_set_style_text_font(s_playLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(s_playLbl);

    lv_obj_t* nextBtn = m3PillBtn(body, LV_SYMBOL_NEXT, M3Color::SURFACE_HIGH, M3Color::TEXT_PRIMARY, 44, 38);
    lv_obj_align(nextBtn, LV_ALIGN_BOTTOM_MID, 62, -2);
    lv_obj_add_event_cb(nextBtn, nextCb, LV_EVENT_CLICKED, nullptr);

    s_lastTickMs = millis();
    refreshMusicUi();
}

void tickAppMusic() {
    if (!s_playing || !s_progBar) return;
    uint32_t now = millis();
    if (now - s_lastTickMs >= 1000) {
        s_lastTickMs += 1000;
        s_posSec++;
        if (s_posSec >= s_tracks[s_trackIdx].durationSec) {
            s_trackIdx = (s_trackIdx + 1) % 3;
            s_posSec = 0;
        }
        refreshMusicUi();
    }
}
