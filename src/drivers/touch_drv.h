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
bool touchReadLightPoint(TouchPoint& out); // Feather-light read (no 2-sample reject) for trackpad
void touchCalibrateInteractive();
bool touchIsCalibrated();

// Touch sensitivity 0..100% (higher = lighter tap registers)
void touchSetSensitivity(uint8_t percent, bool saveNvs = true);
uint8_t touchGetSensitivity();
uint16_t touchGetZThreshold();

// Programmatic / Remote touch injection (thread-safe, consumed by LVGL indev)
void touchInjectTap(int16_t x, int16_t y);
void touchInjectPointer(bool pressed, int16_t x, int16_t y);
void touchInjectSwipe(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t durationMs = 220);
