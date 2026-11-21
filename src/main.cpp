#include <Arduino.h>
#include <lvgl.h>

#include "config/hw_config.h"
#include "drivers/display_drv.h"
#include "drivers/touch_drv.h"
#include "drivers/wifi_mgr.h"
#include "drivers/time_mgr.h"
#include "drivers/notes_store.h"
#include "theme/m3_theme.h"
#include "components/m3_keyboard.h"
#include "ui/home_screen.h"
#include "ui/status_bar.h"
#include "ui/nav_bar.h"
#include "ui/quick_settings.h"
#include "ui/app_manager.h"

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
    wifiMgrInit();
    timeMgrInit();
    notesStoreInit();
    homeScreenInit();
    appManagerInit();
    statusBarInit();
    navBarInit();
    quickSettingsInit();

    Serial.printf("[BOOT] Ready %ux%u, Apps: %u, Free Heap: %lu B\n",
                  displayWidth(), displayHeight(),
                  (unsigned)appGetCount(),
                  (unsigned long)ESP.getFreeHeap());
}

void loop() {
    m3SyncSysTick();

    if (wifiMgrTick()) {
        statusBarRefresh();
        if (quickSettingsIsOpen()) quickSettingsRefresh();
    }

    uint8_t timeChg = timeMgrTick();
    if (timeChg & (TIME_CHG_MINUTE | TIME_CHG_DATE | TIME_CHG_SYNC | TIME_CHG_TZ)) {
        statusBarRefresh();
        homeScreenRefreshDate();
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
