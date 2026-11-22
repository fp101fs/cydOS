#include "launcher_home_sync.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef LAUNCHER_HOME_SYNC_HOST_TEST
#include "launcher_app_registry.h"
#include "launcher_ui_bridge.h"
#include "nvs_helpers.h"
#include "../apps/app_registry.h"
#include "../components/m3_keyboard.h"
#include "../components/m3_modal.h"
#include "../components/m3_widgets.h"
#include "../theme/m3_theme.h"
#include "../ui/app_manager.h"
#include "../ui/home_screen.h"
#include <lvgl.h>
#endif

namespace {

struct PalettePair {
    uint32_t accent;
    uint32_t container;
};

// Curated Material 3 Pixel-style tile palette for dynamically installed Launcher Apps.
constexpr PalettePair kAppPalette[] = {
    {0x8AB4F8, 0x2563EB}, // Pixel Blue
    {0x81C995, 0x15803D}, // Emerald
    {0xC5B4E3, 0x7C3AED}, // Violet
    {0x78D9EC, 0x0891B2}, // Cyan
    {0xFDD663, 0xD97706}, // Amber
    {0xF28B82, 0xE11D48}, // Rose
    {0x5EEAD4, 0x0F766E}, // Teal
    {0xFDBA74, 0xEA580C}, // Tangerine
};

uint32_t fnv1a32(const char* s) {
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619u;
    }
    return h;
}

bool containsStr(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

void launcherHomePickColors(const char* label, uint32_t* outAccentHex, uint32_t* outContainerHex) {
    const size_t count = sizeof(kAppPalette) / sizeof(kAppPalette[0]);
    const PalettePair& pair = kAppPalette[fnv1a32(label) % count];
    if (outAccentHex) *outAccentHex = pair.accent;
    if (outContainerHex) *outContainerHex = pair.container;
}

void launcherHomeMakeDynamicId(const char* label, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    snprintf(out, outSize, "lapp_%s", (label && label[0]) ? label : "app");
}

void launcherHomeReconcileOrder(std::vector<std::string>& recencyOrder,
                                const std::vector<std::string>& installedLabels,
                                const char* changedLabel,
                                bool* outNewAppAdded) {
    bool newAdded = false;

    // 1. Prune entries no longer installed (and deduplicate)
    std::vector<std::string> cleaned;
    cleaned.reserve(installedLabels.size());
    for (const std::string& lbl : recencyOrder) {
        if (!lbl.empty() && containsStr(installedLabels, lbl) && !containsStr(cleaned, lbl)) {
            cleaned.push_back(lbl);
        }
    }

    // 2. If a specific newly installed label was reported and isn't tracked yet, put it at #1
    if (changedLabel && changedLabel[0]) {
        std::string changed(changedLabel);
        if (containsStr(installedLabels, changed) && !containsStr(cleaned, changed)) {
            cleaned.insert(cleaned.begin(), changed);
            newAdded = true;
        }
    }

    // 3. Any remaining installed apps not yet in recencyOrder (e.g. WebUI/external install)
    //    are inserted at the front (newest first).
    for (const std::string& lbl : installedLabels) {
        if (!lbl.empty() && !containsStr(cleaned, lbl)) {
            cleaned.insert(cleaned.begin(), lbl);
            newAdded = true;
        }
    }

    recencyOrder.swap(cleaned);
    if (outNewAppAdded) *outNewAppAdded = newAdded;
}

#ifndef LAUNCHER_HOME_SYNC_HOST_TEST

namespace {

constexpr const char* kCfgNs = "l_cfg";
constexpr const char* kHomeOrderKey = "homeOrder";

std::vector<std::string> s_recencyOrder;
bool s_homeReady = false;

// Async action payload buffers (single UI thread, one action at a time)
char s_pendingLaunchLabel[16] = {};
char s_pendingActionLabel[16] = {};
char s_pendingActionName[24] = {};

// Rename modal state
lv_obj_t* s_renameBackdrop = nullptr;
lv_obj_t* s_renameTa = nullptr;
char s_renameLabel[16] = {};

void loadRecencyOrderFromNvs() {
    s_recencyOrder.clear();
    String stored = lnvs::getString(kCfgNs, kHomeOrderKey, 255);
    int start = 0;
    while (start < static_cast<int>(stored.length())) {
        int comma = stored.indexOf(',', start);
        String tok = (comma >= 0) ? stored.substring(start, comma) : stored.substring(start);
        tok.trim();
        if (!tok.isEmpty()) {
            std::string s(tok.c_str());
            if (!containsStr(s_recencyOrder, s)) {
                s_recencyOrder.push_back(s);
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }
}

void saveRecencyOrderToNvs() {
    String encoded;
    for (const std::string& lbl : s_recencyOrder) {
        if (lbl.empty()) continue;
        if (!encoded.isEmpty()) encoded += ",";
        encoded += lbl.c_str();
    }
    lnvs::Handle handle(kCfgNs, true);
    if (!handle) return;
    if (encoded.isEmpty()) {
        lnvs::eraseKey(handle.raw(), kHomeOrderKey);
    } else {
        lnvs::setString(handle.raw(), kHomeOrderKey, encoded.c_str());
    }
    handle.commit();
}

void dynamicLaunchHandler(const AppMeta* meta) {
    if (!meta || !meta->launcherLabel || !meta->launcherLabel[0]) return;
    launcherHomeLaunchApp(meta->launcherLabel);
}

void asyncLaunchCb(void* arg) {
    (void)arg;
    if (!s_pendingLaunchLabel[0]) return;
    char label[16];
    strncpy(label, s_pendingLaunchLabel, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    s_pendingLaunchLabel[0] = '\0';
    launcherBootAppByLabel(label);
}

void closeRenameModal() {
    m3KeyboardHide();
    if (s_renameBackdrop) {
        lv_obj_del(s_renameBackdrop);
        s_renameBackdrop = nullptr;
    }
    s_renameTa = nullptr;
    s_renameLabel[0] = '\0';
}

void commitRename(const char* newNameRaw) {
    char label[16];
    strncpy(label, s_renameLabel, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';

    String trimmed(newNameRaw ? newNameRaw : "");
    trimmed.trim();
    closeRenameModal();

    if (!label[0] || trimmed.isEmpty()) return;
    if (launcherRenameAppByLabel(label, trimmed)) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Renamed to %s", trimmed.c_str());
        m3ShowToast(msg, 1500);
    }
}

void renameKbdEnterCb(const char* text) {
    commitRename(text);
}

void renameSaveBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    const char* txt = s_renameTa ? lv_textarea_get_text(s_renameTa) : "";
    commitRename(txt);
}

void renameCancelBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    closeRenameModal();
}

void asyncShowActionsCb(void* arg) {
    (void)arg;
    if (!s_pendingActionLabel[0]) return;

    String label(s_pendingActionLabel);
    String name(s_pendingActionName[0] ? s_pendingActionName : s_pendingActionLabel);
    s_pendingActionLabel[0] = '\0';
    s_pendingActionName[0] = '\0';

    String subtitle = String("Installed App (") + label + ")";
    std::vector<LauncherUiOption> opts;
    opts.push_back({String(LV_SYMBOL_PLAY "  Launch App"), [label]() {
        launcherBootAppByLabel(label.c_str());
    }});
    opts.push_back({String(LV_SYMBOL_EDIT "  Rename App"), [label, name]() {
        launcherHomePromptRename(label.c_str(), name.c_str());
    }});
    opts.push_back({String(LV_SYMBOL_TRASH "  Uninstall App"), [label]() {
        launcherDeleteAppByLabel(label.c_str(), false, false);
    }});
    opts.push_back({String(LV_SYMBOL_DOWNLOAD "  Open in App Store"), []() {
        appManagerLaunchById("app_store");
    }});

    launcherUiSelectOption(name.c_str(), opts, subtitle.c_str());
}

void syncRegistryFromInstalled(const char* changedLabel, bool rebuildHome, bool forceFirstPage) {
    std::vector<LauncherAppMetadata> installed = launcherListInstalledApps();
    std::vector<std::string> installedLabels;
    installedLabels.reserve(installed.size());
    for (const LauncherAppMetadata& app : installed) {
        installedLabels.push_back(std::string(app.label.c_str()));
    }

    std::vector<std::string> prevOrder = s_recencyOrder;
    bool newAppAdded = false;
    launcherHomeReconcileOrder(s_recencyOrder, installedLabels, changedLabel, &newAppAdded);
    if (s_recencyOrder != prevOrder) {
        saveRecencyOrderToNvs();
    }

    appClearDynamic();

    int16_t slotOrder = 1; // Slot #1..N right after App Store (order = 0), newest first
    for (const std::string& lbl : s_recencyOrder) {
        const LauncherAppMetadata* found = nullptr;
        for (const LauncherAppMetadata& app : installed) {
            if (app.label == lbl.c_str()) {
                found = &app;
                break;
            }
        }
        if (!found) continue;

        char idBuf[24];
        launcherHomeMakeDynamicId(found->label.c_str(), idBuf, sizeof(idBuf));
        uint32_t accent = M3Color::PRIMARY;
        uint32_t container = 0x2563EB;
        launcherHomePickColors(found->label.c_str(), &accent, &container);

        appRegisterDynamic(idBuf,
                           found->name.c_str(),
                           found->label.c_str(),
                           LV_SYMBOL_PLAY,
                           accent,
                           container,
                           slotOrder++);
    }

    if (rebuildHome && s_homeReady) {
        homeScreenRebuildGrid(forceFirstPage || newAppAdded);
    }
}

} // namespace

void launcherHomeSyncInit() {
    loadRecencyOrderFromNvs();
    appSetDynamicLaunchHandler(dynamicLaunchHandler);
    launcherUiSetAppsChangedCb(launcherHomeSyncOnAppsChanged);
    syncRegistryFromInstalled(nullptr, false, false);
    s_homeReady = true;
}

void launcherHomeSyncRefresh(bool rebuildHome, bool goFirstPage) {
    syncRegistryFromInstalled(nullptr, rebuildHome, goFirstPage);
}

void launcherHomeSyncOnAppsChanged(const char* changedLabel) {
    syncRegistryFromInstalled(changedLabel, true, false);
}

void launcherHomeLaunchApp(const char* label) {
    if (!label || !label[0]) return;
    strncpy(s_pendingLaunchLabel, label, sizeof(s_pendingLaunchLabel) - 1);
    s_pendingLaunchLabel[sizeof(s_pendingLaunchLabel) - 1] = '\0';
    lv_async_call(asyncLaunchCb, nullptr);
}

void launcherHomeShowAppActions(const char* label, const char* displayName) {
    if (!label || !label[0]) return;
    strncpy(s_pendingActionLabel, label, sizeof(s_pendingActionLabel) - 1);
    s_pendingActionLabel[sizeof(s_pendingActionLabel) - 1] = '\0';
    strncpy(s_pendingActionName, (displayName && displayName[0]) ? displayName : label,
            sizeof(s_pendingActionName) - 1);
    s_pendingActionName[sizeof(s_pendingActionName) - 1] = '\0';
    lv_async_call(asyncShowActionsCb, nullptr);
}

void launcherHomePromptRename(const char* label, const char* currentName) {
    if (!label || !label[0]) return;
    closeRenameModal();

    strncpy(s_renameLabel, label, sizeof(s_renameLabel) - 1);
    s_renameLabel[sizeof(s_renameLabel) - 1] = '\0';

    lv_obj_t* top = lv_layer_top();
    s_renameBackdrop = lv_obj_create(top);
    lv_obj_set_size(s_renameBackdrop, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_set_pos(s_renameBackdrop, 0, 0);
    lv_obj_set_style_bg_color(s_renameBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_renameBackdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_renameBackdrop, 0, 0);
    lv_obj_set_style_radius(s_renameBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_renameBackdrop, 0, 0);
    lv_obj_clear_flag(s_renameBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_renameBackdrop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* card = lv_obj_create(s_renameBackdrop);
    lv_obj_set_size(card, 212, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(card, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "Rename App");
    lv_obj_set_style_text_color(title, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    s_renameTa = lv_textarea_create(card);
    lv_obj_set_width(s_renameTa, lv_pct(100));
    lv_textarea_set_one_line(s_renameTa, true);
    lv_textarea_set_max_length(s_renameTa, 20);
    lv_textarea_set_text(s_renameTa, (currentName && currentName[0]) ? currentName : label);
    lv_obj_set_style_bg_color(s_renameTa, lv_color_hex(m3SurfaceHex()), 0);
    lv_obj_set_style_text_color(s_renameTa, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_border_color(s_renameTa, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_border_width(s_renameTa, 1, 0);
    lv_obj_set_style_radius(s_renameTa, 10, 0);

    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancelBtn = lv_btn_create(row);
    lv_obj_set_height(cancelBtn, 30);
    lv_obj_set_style_radius(cancelBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
    lv_obj_set_style_shadow_width(cancelBtn, 0, 0);
    lv_obj_add_event_cb(cancelBtn, renameCancelBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_set_style_text_color(cancelLbl, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);

    lv_obj_t* saveBtn = lv_btn_create(row);
    lv_obj_set_height(saveBtn, 30);
    lv_obj_set_style_radius(saveBtn, M3Dim::RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(saveBtn, lv_color_hex(M3Color::PRIMARY), 0);
    lv_obj_set_style_shadow_width(saveBtn, 0, 0);
    lv_obj_add_event_cb(saveBtn, renameSaveBtnCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "Save");
    lv_obj_set_style_text_color(saveLbl, lv_color_hex(M3Color::ON_PRIMARY), 0);
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(saveLbl);

    m3KeyboardShow(s_renameTa, renameKbdEnterCb);
}

#endif // !LAUNCHER_HOME_SYNC_HOST_TEST
