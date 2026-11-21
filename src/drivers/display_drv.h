#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

extern TFT_eSPI tft;

void displayInit();
uint16_t displayWidth();
uint16_t displayHeight();

// Brightness 10..100%
void displaySetBrightness(uint8_t percent);
uint8_t displayGetBrightness();

// Active-LOW onboard RGB LED (0..255)
void hwRgbLed(uint8_t r, uint8_t g, uint8_t b);
bool hwBootPressed();
