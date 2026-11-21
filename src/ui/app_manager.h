#pragma once

#include "../apps/app_registry.h"
#include <lvgl.h>

void appManagerInit();
void appManagerLaunch(const AppMeta* meta);
void appManagerLaunchById(const char* id);
void appManagerClose();
bool appManagerHasActiveApp();
const char* appManagerGetActiveId();
const char* appManagerGetActiveName();
lv_obj_t* appManagerGetHeader();
void appManagerRefreshTheme();
void appManagerLoop();
