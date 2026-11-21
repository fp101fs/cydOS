#pragma once

#include "../apps/app_registry.h"
#include <lvgl.h>

void appManagerInit();
void appManagerLaunch(AppId id);
void appManagerClose();
bool appManagerHasActiveApp();
void appManagerLoop();
