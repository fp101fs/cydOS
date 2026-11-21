#pragma once

#include <lvgl.h>

typedef void (*M3KbdEnterCb)(const char* text);

void m3KeyboardInit();
void m3KeyboardAttach(lv_obj_t* textarea, M3KbdEnterCb onEnter = nullptr);
void m3KeyboardShow(lv_obj_t* textarea, M3KbdEnterCb onEnter = nullptr);
void m3KeyboardHide();
bool m3KeyboardIsOpen();
void m3KeyboardRefreshTheme();
