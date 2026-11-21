#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct TouchPoint {
    bool pressed;
    int16_t x;
    int16_t y;
};

void touchInit();
bool touchReadPoint(TouchPoint& out);
void touchCalibrateInteractive();
bool touchIsCalibrated();
