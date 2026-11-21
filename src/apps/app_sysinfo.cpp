#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../drivers/display_drv.h"
#include <Arduino.h>
#include <stdio.h>

static lv_obj_t* s_heapLbl = nullptr;
static lv_obj_t* s_minHeapLbl = nullptr;
static lv_obj_t* s_uptimeLbl = nullptr;
static uint32_t s_lastUpdateMs = 0;

static void sysInfoDeleteCb(lv_event_t* e) {
    (void)e;
    s_heapLbl = nullptr;
    s_minHeapLbl = nullptr;
    s_uptimeLbl = nullptr;
}

static void refreshSysInfo() {
    if (!s_heapLbl) return;
    char buf[32];

    snprintf(buf, sizeof(buf), "%lu / %lu KB",
             (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(ESP.getHeapSize() / 1024));
    lv_label_set_text(s_heapLbl, buf);

    if (s_minHeapLbl) {
        snprintf(buf, sizeof(buf), "%lu KB (max %lu KB)",
                 (unsigned long)(ESP.getMinFreeHeap() / 1024),
                 (unsigned long)(ESP.getMaxAllocHeap() / 1024));
        lv_label_set_text(s_minHeapLbl, buf);
    }

    uint32_t sec = millis() / 1000;
    snprintf(buf, sizeof(buf), "%lum %02lus", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    lv_label_set_text(s_uptimeLbl, buf);
}

void buildAppSysInfo(lv_obj_t* body) {
    lv_obj_add_event_cb(body, sysInfoDeleteCb, LV_EVENT_DELETE, nullptr);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 6, 0);

    lv_obj_t* card = m3Card(body, LV_PCT(100), LV_SIZE_CONTENT, 16);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, 0);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s (Rev %u)", ESP.getChipModel(), ESP.getChipRevision());
    m3InfoRow(card, "SoC", buf);

    snprintf(buf, sizeof(buf), "%lu MHz (2 cores)", (unsigned long)ESP.getCpuFreqMHz());
    m3InfoRow(card, "CPU", buf);

    m3InfoRow(card, "Heap Free", "...", &s_heapLbl);
    m3InfoRow(card, "Heap Min", "...", &s_minHeapLbl);

    snprintf(buf, sizeof(buf), "%ux%u Portrait", displayWidth(), displayHeight());
    m3InfoRow(card, "Display", buf);

    snprintf(buf, sizeof(buf), "%u MB", (unsigned int)(ESP.getFlashChipSize() / (1024 * 1024)));
    m3InfoRow(card, "Flash", buf);

    m3InfoRow(card, "Uptime", "...", &s_uptimeLbl);

    snprintf(buf, sizeof(buf), "%u%% (USB)", g_sys.batteryPct);
    m3InfoRow(card, "Power", buf);

    s_lastUpdateMs = 0;
    refreshSysInfo();
}

void tickAppSysInfo() {
    uint32_t now = millis();
    if (now - s_lastUpdateMs >= 1000) {
        s_lastUpdateMs = now;
        refreshSysInfo();
    }
}

REGISTER_APP(sysinfo, "sysinfo", "Sys Info", LV_SYMBOL_CHARGE, 0x78D9EC, 0x0E7490, buildAppSysInfo, tickAppSysInfo, 170);
