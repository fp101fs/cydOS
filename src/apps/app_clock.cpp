#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../drivers/time_mgr.h"
#include "../ui/status_bar.h"
#include "../ui/home_screen.h"
#include <Arduino.h>
#include <stdio.h>

static lv_obj_t* s_timeCard  = nullptr;
static lv_obj_t* s_clockBig  = nullptr;
static lv_obj_t* s_dateLbl   = nullptr;
static lv_obj_t* s_tzCard    = nullptr;
static lv_obj_t* s_tzLbl     = nullptr;
static lv_obj_t* s_minusBtn  = nullptr;
static lv_obj_t* s_plusBtn   = nullptr;
static lv_obj_t* s_syncBtn   = nullptr;
static lv_obj_t* s_cal       = nullptr;

static uint16_t s_calYear    = 0;
static uint8_t  s_calMonth   = 0;
static uint8_t  s_calDay     = 0;
static uint8_t  s_lastSec    = 255;
static uint8_t  s_lastMin    = 255;
static uint8_t  s_lastHour   = 255;
static bool     s_lastSynced = false;
static bool     s_lastSyncing = false;
static int16_t  s_lastTzMin  = 32767;

static void refreshClock() {
    if (!s_clockBig) return;

    char buf[40];
    timeMgrFormatHHMMSS(buf, sizeof(buf));
    lv_label_set_text(s_clockBig, buf);

    if (s_dateLbl) {
        timeMgrFormatLongDate(buf, sizeof(buf));
        lv_label_set_text(s_dateLbl, buf);
    }

    if (s_tzLbl) {
        char tzBuf[16];
        timeMgrFormatTz(tzBuf, sizeof(tzBuf));
        const char* status = g_sys.timeSynced ? "NTP" :
                             (timeMgrIsSyncing() ? "Sync..." : "Offline");
        snprintf(buf, sizeof(buf), "%s  •  %s", tzBuf, status);
        lv_label_set_text(s_tzLbl, buf);
    }

    if (s_cal && g_sys.timeSynced) {
        if (g_sys.year != s_calYear || g_sys.month != s_calMonth || g_sys.day != s_calDay) {
            s_calYear  = g_sys.year;
            s_calMonth = g_sys.month;
            s_calDay   = g_sys.day;
            lv_calendar_set_today_date(s_cal, g_sys.year, g_sys.month, g_sys.day);
            lv_calendar_set_showed_date(s_cal, g_sys.year, g_sys.month);
        }
    }

    s_lastSec     = g_sys.second;
    s_lastMin     = g_sys.minute;
    s_lastHour    = g_sys.hour;
    s_lastSynced  = g_sys.timeSynced;
    s_lastSyncing = timeMgrIsSyncing();
    s_lastTzMin   = g_sys.tzOffsetMin;
}

static void refreshAppClockTheme() {
    if (s_timeCard) {
        lv_obj_set_style_bg_color(s_timeCard,
            lv_color_hex(g_sys.lightMode ? 0xD3E3FD : M3Color::PRIMARY_CONT), 0);
        lv_obj_set_style_border_color(s_timeCard, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_clockBig) {
        lv_obj_set_style_text_color(s_clockBig, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    if (s_dateLbl) {
        lv_obj_set_style_text_color(s_dateLbl, lv_color_hex(m3PrimaryHex()), 0);
    }
    if (s_tzCard) {
        lv_obj_set_style_bg_color(s_tzCard, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_tzCard, lv_color_hex(m3OutlineHex()), 0);
    }
    if (s_tzLbl) {
        lv_obj_set_style_text_color(s_tzLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    if (s_minusBtn) {
        lv_obj_set_style_bg_color(s_minusBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
    }
    if (s_plusBtn) {
        lv_obj_set_style_bg_color(s_plusBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
    }
    if (s_syncBtn) {
        lv_obj_set_style_bg_color(s_syncBtn,
            lv_color_hex(g_sys.lightMode ? 0xD3E3FD : M3Color::PRIMARY_CONT), 0);
    }
    if (s_cal) {
        lv_obj_set_style_bg_color(s_cal, lv_color_hex(m3SurfaceHighHex()), 0);
        lv_obj_set_style_border_color(s_cal, lv_color_hex(m3OutlineHex()), 0);
        lv_obj_set_style_text_color(s_cal, lv_color_hex(m3TextPrimaryHex()), 0);
    }
}

static void tzMinusCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    timeMgrStepTzHours(-1);
    refreshClock();
    statusBarRefresh();
    homeScreenRefreshDate();
}

static void tzPlusCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    timeMgrStepTzHours(1);
    refreshClock();
    statusBarRefresh();
    homeScreenRefreshDate();
}

static void syncBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (timeMgrSyncNow()) {
        m3ShowToast("Syncing NTP...", 1200);
    } else {
        m3ShowToast("Connect Wi-Fi first", 1400);
    }
    refreshClock();
}

static void clockDeleteCb(lv_event_t* e) {
    (void)e;
    s_timeCard = nullptr;
    s_clockBig = nullptr;
    s_dateLbl  = nullptr;
    s_tzCard   = nullptr;
    s_tzLbl    = nullptr;
    s_minusBtn = nullptr;
    s_plusBtn  = nullptr;
    s_syncBtn  = nullptr;
    s_cal      = nullptr;
    s_calYear  = 0;
    s_calMonth = 0;
    s_calDay   = 0;
}

static lv_obj_t* createSmallBtn(lv_obj_t* parent, const char* txt, int16_t w,
                                uint32_t bgHex, uint32_t fgHex, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, 24);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fgHex), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
    return btn;
}

void buildAppClock(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(body, clockDeleteCb, LV_EVENT_DELETE, nullptr);

    s_timeCard = m3Card(body, LV_PCT(100), 66, 16);

    s_clockBig = lv_label_create(s_timeCard);
    lv_obj_set_style_text_font(s_clockBig, &lv_font_montserrat_28, 0);
    lv_obj_align(s_clockBig, LV_ALIGN_CENTER, 0, -6);

    s_dateLbl = lv_label_create(s_timeCard);
    lv_obj_set_style_text_font(s_dateLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_dateLbl, LV_ALIGN_BOTTOM_MID, 0, 2);

    // Timezone offset & manual NTP sync row
    s_tzCard = m3Card(body, LV_PCT(100), 36, 14);
    lv_obj_set_style_pad_hor(s_tzCard, 6, 0);
    lv_obj_set_style_pad_ver(s_tzCard, 4, 0);
    lv_obj_set_flex_flow(s_tzCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_tzCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_minusBtn = createSmallBtn(s_tzCard, "-", 28, m3SurfaceHighestHex(), m3TextPrimaryHex(), tzMinusCb);

    s_tzLbl = lv_label_create(s_tzCard);
    lv_obj_set_style_text_font(s_tzLbl, &lv_font_montserrat_12, 0);

    s_plusBtn = createSmallBtn(s_tzCard, "+", 28, m3SurfaceHighestHex(), m3TextPrimaryHex(), tzPlusCb);
    s_syncBtn = createSmallBtn(s_tzCard, LV_SYMBOL_REFRESH, 30, M3Color::PRIMARY_CONT, M3Color::PRIMARY, syncBtnCb);

    s_cal = lv_calendar_create(body);
    lv_obj_set_size(s_cal, LV_PCT(100), 140);
    lv_calendar_set_today_date(s_cal, g_sys.year, g_sys.month, g_sys.day);
    lv_calendar_set_showed_date(s_cal, g_sys.year, g_sys.month);
    lv_obj_set_style_border_width(s_cal, 1, 0);
    lv_obj_set_style_radius(s_cal, 14, 0);

    s_calYear  = 0;
    s_calMonth = 0;
    s_calDay   = 0;
    s_lastSec  = 255;

    refreshAppClockTheme();
    refreshClock();
}

void tickAppClock() {
    if (!s_clockBig) return;
    if (g_sys.second != s_lastSec ||
        g_sys.minute != s_lastMin ||
        g_sys.hour != s_lastHour ||
        g_sys.timeSynced != s_lastSynced ||
        timeMgrIsSyncing() != s_lastSyncing ||
        g_sys.tzOffsetMin != s_lastTzMin) {
        refreshClock();
    }
}

REGISTER_APP_THEMED(clock, "clock", "Clock", LV_SYMBOL_BELL, 0x8AB4F8, 0x3B82F6,
                    buildAppClock, tickAppClock, refreshAppClockTheme, 140);
