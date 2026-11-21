#include "app_registry.h"
#include "../theme/m3_theme.h"

void buildAppSettings(lv_obj_t* body);
void buildAppMusic(lv_obj_t* body);
void tickAppMusic();
void buildAppFiles(lv_obj_t* body);
void buildAppGallery(lv_obj_t* body);
void buildAppBrowser(lv_obj_t* body);
void buildAppTerminal(lv_obj_t* body);
void buildAppWifi(lv_obj_t* body);
void buildAppBluetooth(lv_obj_t* body);
void buildAppSysInfo(lv_obj_t* body);
void tickAppSysInfo();
void buildAppCalculator(lv_obj_t* body);
void buildAppNotes(lv_obj_t* body);
void buildAppAbout(lv_obj_t* body);
void buildAppClock(lv_obj_t* body);
void tickAppClock();

static const AppMeta s_apps[APP_COUNT] = {
    { APP_SETTINGS,   "Settings",    LV_SYMBOL_SETTINGS,  0x8AB4F8, 0x1D3557, buildAppSettings,   nullptr },
    { APP_THEME,      "Theme",       LV_SYMBOL_EYE_OPEN,  0xFDD663, 0x453917, nullptr,            nullptr },
    { APP_MUSIC,      "Music",       LV_SYMBOL_AUDIO,     0xF28B82, 0x4A2226, buildAppMusic,      tickAppMusic },
    { APP_FILES,      "Files",       LV_SYMBOL_DIRECTORY, 0xFDD663, 0x453917, buildAppFiles,      nullptr },
    { APP_GALLERY,    "Gallery",     LV_SYMBOL_IMAGE,     0x81C995, 0x1B4332, buildAppGallery,    nullptr },
    { APP_BROWSER,    "Browser",     LV_SYMBOL_GPS,       0x78D9EC, 0x173F49, buildAppBrowser,    nullptr },
    { APP_TERMINAL,   "Terminal",    LV_SYMBOL_KEYBOARD,  0xA8DAB5, 0x1C3B28, buildAppTerminal,   nullptr },
    { APP_WIFI,       "Wi-Fi",       LV_SYMBOL_WIFI,      0x8AB4F8, 0x1A3252, buildAppWifi,       nullptr },
    { APP_BLUETOOTH,  "Bluetooth",   LV_SYMBOL_BLUETOOTH, 0xC5B4E3, 0x362B4E, buildAppBluetooth,  nullptr },
    { APP_SYSINFO,    "Sys Info",    LV_SYMBOL_CHARGE,    0x78D9EC, 0x153944, buildAppSysInfo,    tickAppSysInfo },
    { APP_CALCULATOR, "Calc",        LV_SYMBOL_PLUS,      0xC5B4E3, 0x3A2F52, buildAppCalculator, nullptr },
    { APP_NOTES,      "Notes",       LV_SYMBOL_EDIT,      0xFDD663, 0x423819, buildAppNotes,      nullptr },
    { APP_ABOUT,      "About",       LV_SYMBOL_HOME,      0x81C995, 0x1D3F2E, buildAppAbout,      nullptr },
    { APP_CLOCK,      "Clock",       LV_SYMBOL_BELL,      0x8AB4F8, 0x1D3557, buildAppClock,      tickAppClock }
};

const AppMeta* appGetMeta(AppId id) {
    if (id >= APP_COUNT) return nullptr;
    return &s_apps[id];
}
