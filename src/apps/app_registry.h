#pragma once

#ifndef APP_REGISTRY_HOST_TEST
#include <lvgl.h>
#endif
#include <stdint.h>

constexpr uint8_t APP_MAX_COUNT   = 40;
constexpr uint8_t APP_MAX_DYNAMIC = 16;

struct AppMeta {
    const char* id;
    const char* name;
    const char* icon;
    uint32_t accentHex;
    uint32_t containerHex;
    void (*buildFn)(lv_obj_t* body);
    void (*tickFn)();
    int16_t order;
    void (*actionFn)();
    void (*themeFn)();
    const lv_img_dsc_t* iconImg;
    const char* launcherLabel; // non-null for runtime-installed Launcher Apps
};

struct AppRegistrar {
    explicit AppRegistrar(const AppMeta* meta);
};

using AppDynamicLaunchFn = void (*)(const AppMeta* meta);

void appRegister(const AppMeta* meta);
const AppMeta* appRegisterDynamic(const char* id,
                                  const char* name,
                                  const char* launcherLabel,
                                  const char* icon,
                                  uint32_t accentHex,
                                  uint32_t containerHex,
                                  int16_t order);
bool appUnregisterById(const char* id);
void appClearDynamic();
uint8_t appGetDynamicCount();
bool appIsDynamic(const AppMeta* meta);

void appSetDynamicLaunchHandler(AppDynamicLaunchFn fn);
AppDynamicLaunchFn appGetDynamicLaunchHandler();

uint8_t appGetCount();
const AppMeta* appGetAt(uint8_t index);
const AppMeta* appFindById(const char* id);

#define REGISTER_APP_EX(varId, idStr, nameStr, iconStr, accent, container, build, tick, ord, action) \
    static const AppMeta s_appMeta_##varId = {                                                        \
        idStr, nameStr, iconStr, accent, container, build, tick, ord, action, nullptr, nullptr,       \
        nullptr                                                                                       \
    };                                                                                                \
    static const AppRegistrar s_appReg_##varId(&s_appMeta_##varId)

#define REGISTER_APP(varId, idStr, nameStr, iconStr, accent, container, build, tick, ord) \
    REGISTER_APP_EX(varId, idStr, nameStr, iconStr, accent, container, build, tick, ord, nullptr)

#define REGISTER_APP_THEMED(varId, idStr, nameStr, iconStr, accent, container, build, tick, theme, ord) \
    static const AppMeta s_appMeta_##varId = {                                                           \
        idStr, nameStr, iconStr, accent, container, build, tick, ord, nullptr, theme, nullptr, nullptr  \
    };                                                                                                   \
    static const AppRegistrar s_appReg_##varId(&s_appMeta_##varId)

#define REGISTER_APP_THEMED_IMG(varId, idStr, nameStr, iconStr, iconImgPtr, accent, container, build, tick, theme, ord) \
    static const AppMeta s_appMeta_##varId = {                                                                           \
        idStr, nameStr, iconStr, accent, container, build, tick, ord, nullptr, theme, iconImgPtr, nullptr                \
    };                                                                                                                   \
    static const AppRegistrar s_appReg_##varId(&s_appMeta_##varId)
