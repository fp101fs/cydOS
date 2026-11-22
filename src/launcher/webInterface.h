#ifndef LAUNCHER_WEB_INTERFACE_H
#define LAUNCHER_WEB_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

// Vendored from bmorcelli/Launcher (src/webInterface.h), adapted for non-blocking
// LVGL integration in pixel-cyd (Tools -> Web Upload (WUI)).

enum class LauncherWebUiState : uint8_t {
    Stopped = 0,
    Idle,
    Uploading,
    Flashing,
    Success,
    Error
};

struct LauncherWebUiStatus {
    bool running = false;
    bool apMode = false;
    LauncherWebUiState state = LauncherWebUiState::Stopped;
    uint8_t progressPct = 0;
    uint32_t bytesDone = 0;
    uint32_t bytesTotal = 0;
    uint32_t eventSeq = 0;
    char url[48] = {};
    char ssid[36] = {};
    char statusText[64] = {};
    char lastInstalledLabel[16] = {};
    char lastInstalledName[24] = {};
};

#ifndef LAUNCHER_WEBUI_HOST_TEST

#include <Arduino.h>

// Start the local WebUI HTTP server on port 80.
// apMode == false: serves on the current Wi-Fi STA IP (requires WiFi connected).
// apMode == true:  starts a SoftAP ("Pixel-CYD-Store" at 192.168.4.1).
bool launcherWebUiStart(bool apMode, String *errorOut = nullptr);

// Stop the HTTP server (and stop SoftAP if started in AP mode), freeing scratch RAM.
void launcherWebUiStop();

// Returns true while the HTTP server is active.
bool launcherWebUiIsRunning();

// Thread-safe snapshot of current WebUI URL, network info, and live upload/flash status.
LauncherWebUiStatus launcherWebUiGetStatus();

// Call periodically from the LVGL main thread (e.g., 250 ms timer) to process
// cross-thread events queued by HTTP handlers: Home Screen sync, SD install, or reboot.
// Returns true if an App list / status change occurred.
bool launcherWebUiPoll();

#endif // !LAUNCHER_WEBUI_HOST_TEST

#endif // LAUNCHER_WEB_INTERFACE_H
