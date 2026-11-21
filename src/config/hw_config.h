#pragma once

#include <Arduino.h>

// Backlight pins (GPIO 27 on dual-USB CYD, GPIO 21 on classic single-USB CYD)
#ifndef CYD_BL_PIN
#define CYD_BL_PIN 27
#endif
#define CYD_BL_PIN_ALT 21

#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ    5000
#define BL_PWM_RES     8

// Onboard BOOT button & active-LOW RGB LED
#define PIN_BOOT  0
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

// Dedicated XPT2046 resistive touch SPI pins
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36

#ifndef TOUCH_Z_MIN
#define TOUCH_Z_MIN 80
#endif
#ifndef TOUCH_SAMPLE_MAX_DELTA
#define TOUCH_SAMPLE_MAX_DELTA 120
#endif

// Default portrait dimensions (verified at runtime via tft.width()/height())
#define DEFAULT_SCREEN_W 240
#define DEFAULT_SCREEN_H 320
