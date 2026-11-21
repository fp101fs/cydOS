#include <Arduino.h>
#include <lvgl.h>

#include "config/hw_config.h"
#include "drivers/display_drv.h"
#include "drivers/touch_drv.h"
#include "theme/m3_theme.h"
#include "components/m3_keyboard.h"
#include "ui/home_screen.h"
#include "ui/status_bar.h"
#include "ui/nav_bar.h"
#include "ui/quick_settings.h"
#include "ui/app_manager.h"

static uint8_t s_lastMinute = 255;
static bool s_bootWasDown = false;

void setup() {
    Serial.begin(115200);
    delay(150);
    Serial.println("\n[BOOT] Pixel 8 CYD UI (LVGL 8.3)");

    lv_init();
    displayInit();
    hwRgbLed(0, 255, 0);
    delay(120);
    hwRgbLed(0, 0, 0);
    touchInit();

    if (hwBootPressed()) {
        Serial.println("[TOUCH] BOOT held -> interactive calibration");
        touchCalibrateInteractive();
    }

    m3ThemeInit();
    homeScreenInit();
    appManagerInit();
    statusBarInit();
    navBarInit();
    quickSettingsInit();

    s_lastMinute = g_sys.minute;
    Serial.printf("[BOOT] Ready %ux%u, Free Heap: %lu B\n",
                  displayWidth(), displayHeight(),
                  (unsigned long)ESP.getFreeHeap());
}

void loop() {
    m3SyncSysTick();

    if (g_sys.minute != s_lastMinute) {
        s_lastMinute = g_sys.minute;
        statusBarRefresh();
        if (quickSettingsIsOpen()) quickSettingsRefresh();
    }

    bool bootDown = hwBootPressed();
    if (bootDown && !s_bootWasDown) {
        if (m3KeyboardIsOpen()) {
            m3KeyboardHide();
        } else if (quickSettingsIsOpen()) {
            quickSettingsClose();
        } else if (appManagerHasActiveApp()) {
            appManagerClose();
        }
    }
    s_bootWasDown = bootDown;

    appManagerLoop();
    lv_timer_handler();
    delay(4);
}
