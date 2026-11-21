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

// Touch sensitivity 0..100% (higher = lighter tap registers)
void touchSetSensitivity(uint8_t percent, bool saveNvs = true);
uint8_t touchGetSensitivity();
uint16_t touchGetZThreshold();
