#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Pure helpers (host-testable without ESP32/LVGL)
void launcherHomePickColors(const char* label, uint32_t* outAccentHex, uint32_t* outContainerHex);
void launcherHomeMakeDynamicId(const char* label, char* out, size_t outSize);

// Reconcile persisted newest-first recency order with currently installed partition labels.
// - If changedLabel is non-empty, exists in installedLabels, and is not yet in recencyOrder,
//   it is prepended to the front (slot #1, newest first) and *outNewAppAdded = true.
// - Labels no longer in installedLabels are pruned.
// - Any newly discovered installedLabels not yet tracked are prepended to the front.
void launcherHomeReconcileOrder(std::vector<std::string>& recencyOrder,
                                const std::vector<std::string>& installedLabels,
                                const char* changedLabel = nullptr,
                                bool* outNewAppAdded = nullptr);

#ifndef LAUNCHER_HOME_SYNC_HOST_TEST

// Initialize Home Screen <-> Launcher sync on boot (call BEFORE homeScreenInit()).
void launcherHomeSyncInit();

// Re-read installed Launcher Apps from flash/NVS, update dynamic tiles in app_registry,
// and optionally rebuild the Home Screen grid live.
void launcherHomeSyncRefresh(bool rebuildHome = true, bool goFirstPage = false);

// Callback registered with launcherUiSetAppsChangedCb().
void launcherHomeSyncOnAppsChanged(const char* changedLabel);

// Launch an installed Launcher App safely (deferred via lv_async_call).
void launcherHomeLaunchApp(const char* label);

// Open Material 3 long-press action menu (Launch, Rename, Uninstall, Open in App Store).
void launcherHomeShowAppActions(const char* label, const char* displayName);

// Open Material 3 rename modal with on-screen keyboard for an installed Launcher App.
void launcherHomePromptRename(const char* label, const char* currentName);

#endif // !LAUNCHER_HOME_SYNC_HOST_TEST
