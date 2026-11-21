#pragma once

#include <lvgl.h>
#include <stdint.h>

enum AppId : uint8_t {
    APP_SETTINGS = 0,
    APP_THEME,
    APP_MUSIC,
    APP_FILES,
    APP_GALLERY,
    APP_BROWSER,
    APP_TERMINAL,
    APP_WIFI,
    APP_BLUETOOTH,
    APP_SYSINFO,
    APP_CALCULATOR,
    APP_NOTES,
    APP_ABOUT,
    APP_CLOCK,
    APP_COUNT
};

struct AppMeta {
    AppId id;
    const char* name;
    const char* icon;
    uint32_t accentHex;
    uint32_t containerHex;
    void (*buildFn)(lv_obj_t* body);
    void (*tickFn)();
};

const AppMeta* appGetMeta(AppId id);
