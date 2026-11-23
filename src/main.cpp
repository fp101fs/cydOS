#include <Arduino.h>
#include <lvgl.h>
#include <esp_bt.h>
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
#include "components/m3_modal.h"
#include "ui/home_screen.h"
#include "ui/status_bar.h"
#include "ui/nav_bar.h"
#include "ui/quick_settings.h"
#include "ui/app_manager.h"
#include "launcher/launcher_ui_bridge.h"
#include "launcher/backup_manager.h"
#include "launcher/onlineLauncher.h"
#include "launcher/launcher_home_sync.h"
#include "apps/app_store.h"

static bool s_bootWasDown = false;

// Increase Arduino loopTask stack from 8 KB to 12 KB so LVGL + mbedTLS HTTPS + ArduinoJson
// in App Store Explore/OTA never overflow the stack while maximizing contiguous heap.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

void setup() {
    // Release unused Classic Bluetooth DRAM (~36 KB) while keeping BLE (NimBLE) intact.
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

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

    Serial.printf("[BOOT] Ready %ux%u, Apps: %u, Free Heap: %lu B (max block %lu B)\n",
                  displayWidth(), displayHeight(),
                  (unsigned)appGetCount(),
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
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
        if (m3ModalIsLocked()) {
            // Protect active flash/partition commits from accidental dismissal.
        } else if (m3KeyboardIsOpen()) {
            m3KeyboardHide();
        } else if (m3ModalIsOpen()) {
            m3ModalClose();
        } else if (quickSettingsIsOpen()) {
            quickSettingsClose();
        } else if (appManagerHasActiveApp()) {
            appManagerClose();
        }
    }
    s_bootWasDown = bootDown;

    if (Serial.available()) {
        int ch = Serial.read();
        auto clearOverlays = [&]() {
            if (m3ModalIsOpen()) m3ModalClose();
            if (m3KeyboardIsOpen()) m3KeyboardHide();
            navBarCloseMenu();
            if (quickSettingsIsOpen()) quickSettingsClose();
        };
        if (ch == 'E') {
            Serial.printf("[TEST] Opening App Store -> Explore (free=%lu maxBlk=%lu)\n",
                          (unsigned long)ESP.getFreeHeap(),
                          (unsigned long)ESP.getMaxAllocHeap());
            appStoreOpenTab(AppStoreTab::Explore);
        } else if (ch == 'S') {
            displayCaptureScreenshotSerial();
        } else if (ch == 'H') {
            clearOverlays();
            if (appManagerHasActiveApp()) appManagerClose();
            homeScreenGoPage(0);
        } else if (ch == '1') {
            clearOverlays();
            if (appManagerHasActiveApp()) appManagerClose();
            homeScreenGoPage(1);
        } else if (ch == '2') {
            clearOverlays();
            if (appManagerHasActiveApp()) appManagerClose();
            homeScreenGoPage(2);
        } else if (ch == 'L') {
            clearOverlays();
            if (appManagerHasActiveApp()) appManagerClose();
            g_sys.lightMode = true;
            m3ApplyThemeAll();
            homeScreenGoPage(0);
        } else if (ch == 'D') {
            clearOverlays();
            g_sys.lightMode = false;
            m3ApplyThemeAll();
        } else if (ch == 'Q') {
            clearOverlays();
            quickSettingsOpen();
        } else if (ch == 'U') {
            clearOverlays();
            navBarOpenMenu();
        } else if (ch == 'M') {
            clearOverlays();
            if (appManagerHasActiveApp()) appManagerClose();
            static const M3ModalBtn kDemoBtns[2] = {
                { "Cancel", 0, 0, nullptr, true },
                { "Continue", M3Color::PRIMARY_CONT, M3Color::PRIMARY, nullptr, true }
            };
            m3ModalShow("Confirm Action",
                        "Material 3 modal dialog with dimmed backdrop and action buttons.",
                        kDemoBtns, 2, M3Color::PRIMARY);
        } else if (ch == 'A') {
            clearOverlays();
            appManagerLaunchById("app_store");
        } else if (ch == 'W') {
            clearOverlays();
            appManagerLaunchById("wifi");
        } else if (ch == 'B') {
            clearOverlays();
            appManagerLaunchById("bluetooth");
        } else if (ch == 'T') {
            clearOverlays();
            appManagerLaunchById("trackpad");
        } else if (ch == 'G') {
            clearOverlays();
            appManagerLaunchById("settings");
        } else if (ch == 'I') {
            clearOverlays();
            appManagerLaunchById("sysinfo");
        } else if (ch == 'K') {
            clearOverlays();
            appManagerLaunchById("browser");
            m3KeyboardShow(nullptr);
        } else if (ch == 'O') {
            char id[24] = "";
            size_t len = 0;
            uint32_t t0 = millis();
            while (len + 1 < sizeof(id) && (millis() - t0) < 250) {
                if (Serial.available()) {
                    int c = Serial.read();
                    if (c == '\n' || c == '\r') break;
                    id[len++] = (char)c;
                }
            }
            id[len] = '\0';
            if (len > 0) {
                clearOverlays();
                appManagerLaunchById(id);
            }
        }
    }

    appManagerLoop();
    lv_timer_handler();
    delay(4);
}
