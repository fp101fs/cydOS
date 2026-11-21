#pragma once

#include <lvgl.h>
#include <functional>

lv_obj_t* m3Card(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius = 16);

lv_obj_t* m3AppHeader(lv_obj_t* parent, const char* title, uint32_t accentHex, lv_event_cb_t backCb, void* userData = nullptr);
void m3AppHeaderRefreshTheme(lv_obj_t* bar);

lv_obj_t* m3ToggleRow(
    lv_obj_t* parent,
    const char* iconSym,
    const char* title,
    const char* subtitle,
    bool initialState,
    uint32_t accentHex,
    lv_event_cb_t onChangeCb,
    void* userData = nullptr
);

lv_obj_t* m3InfoRow(lv_obj_t* parent, const char* label, const char* value, lv_obj_t** outValueLabel = nullptr);

lv_obj_t* m3PillBtn(lv_obj_t* parent, const char* text, uint32_t bgHex, uint32_t fgHex, lv_coord_t w, lv_coord_t h);

void m3ShowToast(const char* text, uint32_t durationMs = 1500);
