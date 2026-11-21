#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include <Arduino.h>
#include <stdio.h>

static lv_obj_t* s_termOut = nullptr;

static void runCmdCb(lv_event_t* e) {
    m3PulseFeedback();
    int cmd = (int)(intptr_t)lv_event_get_user_data(e);
    char buf[192];

    if (cmd == 0) {
        snprintf(buf, sizeof(buf),
                 "$ uname -a\nPixel-CYD 14.0 ESP32-D0WD\n240MHz Xtensa LX6\n\n$ _");
    } else if (cmd == 1) {
        snprintf(buf, sizeof(buf),
                 "$ free -k\nHeap Free: %lu KB\nMin Free:  %lu KB\n\n$ _",
                 (unsigned long)(ESP.getFreeHeap() / 1024),
                 (unsigned long)(ESP.getMinFreeHeap() / 1024));
    } else if (cmd == 2) {
        uint32_t s = millis() / 1000;
        snprintf(buf, sizeof(buf),
                 "$ uptime\nup %lum %lus, 1 user\nload: 0.14, 0.10, 0.08\n\n$ _",
                 (unsigned long)(s / 60), (unsigned long)(s % 60));
    } else {
        snprintf(buf, sizeof(buf),
                 "$ ls /system\nbin/  etc/  fonts/\nframework/  lvgl.so\n\n$ _");
    }
    lv_label_set_text(s_termOut, buf);
}

void buildAppTerminal(lv_obj_t* body) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 8, 0);

    lv_obj_t* win = m3Card(body, LV_PCT(100), 164, 12);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x090B0E), 0);
    lv_obj_set_style_border_color(win, lv_color_hex(0x2D4F38), 0);

    s_termOut = lv_label_create(win);
    lv_label_set_text(s_termOut,
        "pixel@cyd:~$ neofetch\n"
        "OS: Android 14 (CYD)\n"
        "UI: LVGL 8.3 Material 3\n"
        "CPU: ESP32 @ 240MHz\n"
        "Res: 240x320 Portrait\n\n"
        "pixel@cyd:~$ _");
    lv_obj_set_width(s_termOut, 198);
    lv_obj_set_style_text_color(s_termOut, lv_color_hex(0xA8DAB5), 0);
    lv_obj_set_style_text_font(s_termOut, &lv_font_montserrat_12, 0);
    lv_obj_align(s_termOut, LV_ALIGN_TOP_LEFT, 0, 0);

    // Quick command chips
    lv_obj_t* row = lv_obj_create(body);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    const char* cmds[4] = { "uname", "free", "uptime", "ls" };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* b = m3PillBtn(row, cmds[i], M3Color::SURFACE_HIGHEST, 0xA8DAB5, 52, 28);
        lv_obj_add_event_cb(b, runCmdCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

REGISTER_APP(terminal, "terminal", "Terminal", LV_SYMBOL_KEYBOARD, 0xA8DAB5, 0x1C3B28, buildAppTerminal, nullptr, 30);
