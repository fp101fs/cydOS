#pragma once

#include <lvgl.h>
#include <stdint.h>

void homeScreenInit();
void homeScreenRefreshTheme();
void homeScreenRefreshDate();
void homeScreenGoPage(uint8_t page);
void homeScreenRebuildGrid(bool goFirstPage = false);
