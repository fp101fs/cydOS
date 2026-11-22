#include <Arduino.h>
#include <lvgl.h>
#include <esp_sleep.h>

#include "config/hw_config.h"
#include "drivers/display_drv.h"
#include "drivers/touch_drv.h"
#include "drivers/wifi_mgr.h"
#include "drivers/ble_mgr.h"
#include "drivers/time_mgr.h"
#include "drivers/notes_store.h"
#include "drivers/companion_mgr.h"
#include "drivers/screen_mirror.h"
#include "theme/m3_theme.h"
#include "components/m3_widgets.h"
#include "components/m3_keyboard.h"
#include "ui/home_screen.h"
#include "ui/status_bar.h"
#include "ui/nav_bar.h"
#include "ui/quick_settings.h"
#include "ui/app_manager.h"
#include "launcher/launcher_ui_bridge.h"
#include "launcher/backup_manager.h"
#include "launcher/onlineLauncher.h"
#include "launcher/launcher_home_sync.h"

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

    if (hwBootPressed() && esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[TOUCH] BOOT held -> interactive calibration");
        touchCalibrateInteractive();
    }

    m3ThemeInit();
    wifiMgrInit();
    bleMgrInit();
    timeMgrInit();
    notesStoreInit();
    launcherUiLoadState();
    launcherBackupInit();
    launcherOnlineInit();
    launcherHomeSyncInit();
    companionMgrInit();
    screenMirrorInit();
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

    if (bleMgrTick()) {
        statusBarRefresh();
        if (quickSettingsIsOpen()) quickSettingsRefresh();
    }

    if (companionMgrTick()) {
        statusBarRefresh();
    }
    screenMirrorTick();
    char waSender[COMPANION_SENDER_MAX];
    char waPreview[64];
    if (companionConsumeNewAlert(waSender, sizeof(waSender), waPreview, sizeof(waPreview))) {
        char toast[96];
        snprintf(toast, sizeof(toast), "WA %s: %s", waSender, waPreview);
        m3ShowToast(toast, 2400);
        m3PulseFeedback();
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
