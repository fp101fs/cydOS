#ifndef LAUNCHER_UI_BRIDGE_H
#define LAUNCHER_UI_BRIDGE_H

#ifndef LAUNCHER_BRIDGE_HOST_TEST
#include <Arduino.h>
#endif
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Persistent Launcher backend state (stored in NVS namespace "l_cfg").
extern String g_launcherLastInstalledApp;
extern bool g_launcherBootToApp;
extern bool g_launcherAutoBackup;

void launcherUiLoadState();
void launcherUiSaveState();

// --- Progress & Narration Bridge (routes to pixel-cyd's m3_modal) ---

// Open a Material 3 progress modal (starts in indeterminate sweep mode).
void launcherUiBeginProgress(const char *title,
                             const char *stepText = nullptr,
                             uint32_t accentHex = 0,
                             bool cancellable = false,
                             void (*onCancel)() = nullptr);

// Update the active progress modal with plain-English step narration and optional
// determinate percentage (0..100, or < 0 for indeterminate sweep).
// If no progress modal is currently open, opens one automatically.
void launcherUiStep(const char *stepText, int8_t pct = -1, const char *detailText = nullptr);

// Update progress from a byte counter (formats human-readable "420 KB / 1.1 MB" detail).
void launcherUiProgressBytes(size_t current, size_t total, const char *stepText = nullptr);

// Transition the active progress modal to a completed or failed state.
void launcherUiFinish(bool success, const char *headline, const char *detail = nullptr);

// Show an error (transitions open progress modal to failed state, or opens an error modal).
void launcherUiError(const String &msg, const char *title = "Error");

// Show an info/success message (transitions open progress modal to success state, or opens info modal).
void launcherUiInfo(const String &msg, const char *title = "App Store");

// Close any open modal.
void launcherUiClose();

// Cancellation flag helpers for long-running cancellable operations.
bool launcherUiIsCancelled();
void launcherUiResetCancel();

// --- Interactive Modal Prompts (used by partition_install_layout & interactive flows) ---

struct LauncherUiOption {
    String label;
    std::function<void()> operation;
};

// Blocking Material 3 option picker modal (pumps LVGL while waiting for tap).
// Executes the selected option's operation() callback if non-empty.
// Returns the chosen index, or -1 if cancelled.
int launcherUiSelectOption(const char *title,
                           const std::vector<LauncherUiOption> &options,
                           const char *subtitle = nullptr);

// Blocking Material 3 confirmation modal (pumps LVGL while waiting for tap).
// Returns true if user tapped confirmLabel, false on Cancel/close.
bool launcherUiConfirm(const char *title,
                       const char *body,
                       const char *confirmLabel = "Confirm",
                       uint32_t confirmBgHex = 0);

// Cleanly flush state and reboot the ESP32 into the configured boot partition.
bool launcherUiReboot();

// --- Cross-module Hooks (wired by Item 4 backup_manager and Item 5 Home Screen) ---

using LauncherOnAppsChangedCb = void (*)(const char *addedOrChangedLabel);
void launcherUiSetAppsChangedCb(LauncherOnAppsChangedCb cb);
void launcherUiNotifyAppsChanged(const char *label = nullptr);

using LauncherOnAppRenamedCb = void (*)(const String &appNum, const String &newName);
void launcherUiSetAppRenamedCb(LauncherOnAppRenamedCb cb);
void launcherUiNotifyAppRenamed(const String &appNum, const String &newName);

// Format byte count into compact human-readable text ("420 KB", "1.4 MB", "512 B").
void launcherUiFormatBytes(size_t bytes, char *out, size_t outSize);

#endif /* LAUNCHER_UI_BRIDGE_H */
