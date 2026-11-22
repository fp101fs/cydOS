#ifndef LAUNCHER_BRIDGE_HOST_TEST
#include "launcher_ui_bridge.h"
#include "launcher_platform.h"
#include "nvs_helpers.h"
#include "../components/m3_modal.h"
#include "../theme/m3_theme.h"
#include <lvgl.h>
#endif

#include <cstdio>
#include <cstring>

String g_launcherLastInstalledApp = "";
bool g_launcherBootToApp = false;
bool g_launcherAutoBackup = false;

namespace {
constexpr const char *kCfgNs = "l_cfg";
volatile bool s_cancelled = false;
void (*s_userOnCancel)() = nullptr;
LauncherOnAppsChangedCb s_onAppsChanged = nullptr;
LauncherOnAppRenamedCb s_onAppRenamed = nullptr;

void bridgeCancelHandler() {
    s_cancelled = true;
    if (s_userOnCancel) {
        void (*cb)() = s_userOnCancel;
        s_userOnCancel = nullptr;
        cb();
    }
}
} // namespace

void launcherUiFormatBytes(size_t bytes, char *out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (bytes >= 1024UL * 1024UL) {
        uint32_t tenths = static_cast<uint32_t>((bytes * 10ULL) / (1024ULL * 1024ULL));
        snprintf(out, outSize, "%lu.%lu MB",
                 static_cast<unsigned long>(tenths / 10),
                 static_cast<unsigned long>(tenths % 10));
    } else if (bytes >= 1024UL) {
        snprintf(out, outSize, "%lu KB", static_cast<unsigned long>(bytes / 1024UL));
    } else {
        snprintf(out, outSize, "%lu B", static_cast<unsigned long>(bytes));
    }
}

void launcherUiLoadState() {
#ifndef LAUNCHER_BRIDGE_HOST_TEST
    lnvs::Handle handle(kCfgNs, false);
    if (!handle) return;
    g_launcherLastInstalledApp = lnvs::getString(handle.raw(), "lastApp", 31);
    lnvs::getBool(handle.raw(), "bootToApp", g_launcherBootToApp);
    lnvs::getBool(handle.raw(), "autoBackup", g_launcherAutoBackup);
#endif
}

void launcherUiSaveState() {
#ifndef LAUNCHER_BRIDGE_HOST_TEST
    lnvs::Handle handle(kCfgNs, true);
    if (!handle) return;
    lnvs::setString(handle.raw(), "lastApp", g_launcherLastInstalledApp.c_str());
    lnvs::setBool(handle.raw(), "bootToApp", g_launcherBootToApp);
    lnvs::setBool(handle.raw(), "autoBackup", g_launcherAutoBackup);
    handle.commit();
#endif
}

void launcherUiBeginProgress(const char *title,
                             const char *stepText,
                             uint32_t accentHex,
                             bool cancellable,
                             void (*onCancel)()) {
    s_cancelled = false;
    s_userOnCancel = onCancel;
    m3ModalShowProgress(title ? title : "App Store",
                        stepText,
                        accentHex,
                        cancellable,
                        cancellable ? bridgeCancelHandler : nullptr);
}

void launcherUiStep(const char *stepText, int8_t pct, const char *detailText) {
    if (!m3ModalIsProgressOpen()) {
        launcherUiBeginProgress("App Store", stepText, 0, false, nullptr);
        if (pct >= 0 || detailText) {
            m3ModalUpdateProgress(pct, stepText, detailText, true);
        }
        return;
    }
    m3ModalUpdateProgress(pct, stepText, detailText, true);
}

void launcherUiProgressBytes(size_t current, size_t total, const char *stepText) {
    int8_t pct = -1;
    char detail[48];
    if (total > 0) {
        uint64_t scaled = (static_cast<uint64_t>(current) * 100ULL) / static_cast<uint64_t>(total);
        if (scaled > 100ULL) scaled = 100ULL;
        pct = static_cast<int8_t>(scaled);
        char curStr[20];
        char totStr[20];
        launcherUiFormatBytes(current, curStr, sizeof(curStr));
        launcherUiFormatBytes(total, totStr, sizeof(totStr));
        snprintf(detail, sizeof(detail), "%s / %s", curStr, totStr);
    } else {
        launcherUiFormatBytes(current, detail, sizeof(detail));
    }
    launcherUiStep(stepText, pct, detail);
}

bool launcherUiIsProgressOpen() {
    return m3ModalIsProgressOpen();
}

void launcherUiLockProgress() {
    s_userOnCancel = nullptr;
    m3ModalSetProgressLocked(true);
}

void launcherUiFinish(bool success, const char *headline, const char *detail) {
    s_userOnCancel = nullptr;
    m3ModalFinishProgress(success, headline ? headline : (success ? "Done" : "Failed"), detail);
}

void launcherUiFinishActions(bool success,
                             const char *headline,
                             const char *detail,
                             const LauncherUiFinishAction *actions,
                             uint8_t actionCount) {
    s_userOnCancel = nullptr;
    if (!actions || actionCount == 0) {
        m3ModalFinishProgress(success, headline ? headline : (success ? "Done" : "Failed"), detail);
        return;
    }
    M3ModalBtn btns[4];
    uint8_t count = actionCount > 4 ? 4 : actionCount;
    for (uint8_t i = 0; i < count; ++i) {
        btns[i].label = actions[i].label;
        btns[i].bgHex = actions[i].bgHex;
        btns[i].fgHex = actions[i].fgHex;
        btns[i].onClick = actions[i].onClick;
        btns[i].closeOnClick = actions[i].closeOnClick;
    }
    m3ModalFinishProgress(success,
                          headline ? headline : (success ? "Done" : "Failed"),
                          detail,
                          btns,
                          count);
}

void launcherUiFinishWithPrimaryAction(bool success,
                                       const char *headline,
                                       const char *detail,
                                       const char *primaryLabel,
                                       void (*onPrimary)(),
                                       const char *dismissLabel) {
    const LauncherUiFinishAction actions[2] = {
        {dismissLabel ? dismissLabel : "Done", 0, 0, nullptr, true},
        {primaryLabel ? primaryLabel : "OK", M3Color::PRIMARY, 0x0F1117, onPrimary, true},
    };
    launcherUiFinishActions(success, headline, detail, actions, 2);
}

void launcherUiError(const String &msg, const char *title) {
    s_userOnCancel = nullptr;
    const char *hdr = title ? title : "Error";
    if (m3ModalIsProgressOpen()) {
        m3ModalFinishProgress(false, hdr, msg.c_str());
    } else {
        m3ModalShow(hdr, msg.c_str(), nullptr, 0, M3Color::CORAL);
    }
}

void launcherUiInfo(const String &msg, const char *title) {
    s_userOnCancel = nullptr;
    const char *hdr = title ? title : "App Store";
    if (m3ModalIsProgressOpen()) {
        m3ModalFinishProgress(true, hdr, msg.c_str());
    } else {
        m3ModalShow(hdr, msg.c_str(), nullptr, 0, M3Color::PRIMARY);
    }
}

void launcherUiClose() {
    s_userOnCancel = nullptr;
    m3ModalClose();
}

bool launcherUiIsCancelled() { return s_cancelled; }

void launcherUiResetCancel() { s_cancelled = false; }

void launcherUiSetAppsChangedCb(LauncherOnAppsChangedCb cb) { s_onAppsChanged = cb; }

void launcherUiNotifyAppsChanged(const char *label) {
    if (s_onAppsChanged) s_onAppsChanged(label);
}

void launcherUiSetAppRenamedCb(LauncherOnAppRenamedCb cb) { s_onAppRenamed = cb; }

void launcherUiNotifyAppRenamed(const String &appNum, const String &newName) {
    if (s_onAppRenamed) s_onAppRenamed(appNum, newName);
}

#ifndef LAUNCHER_BRIDGE_HOST_TEST

namespace {
struct ConfirmState {
    volatile int result = -1; // -1 pending, 0 cancel, 1 confirm
};
ConfirmState s_confirmState;

void confirmYesCb() { s_confirmState.result = 1; }
void confirmNoCb() { s_confirmState.result = 0; }

struct OptionPickerState {
    volatile int chosen = -2; // -2 pending, -1 cancel, >=0 index
};
OptionPickerState s_pickerState;

void pickerRowCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    s_pickerState.chosen = idx;
}

void pickerCancelCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    s_pickerState.chosen = -1;
}
} // namespace

bool launcherUiConfirm(const char *title,
                       const char *body,
                       const char *confirmLabel,
                       uint32_t confirmBgHex) {
    s_confirmState.result = -1;
    const M3ModalBtn btns[2] = {
        {"Cancel", 0, 0, confirmNoCb, true},
        {confirmLabel ? confirmLabel : "Confirm",
         confirmBgHex ? confirmBgHex : M3Color::PRIMARY_CONT,
         confirmBgHex ? M3Color::BG_DARK : M3Color::PRIMARY,
         confirmYesCb,
         true},
    };
    m3ModalShow(title ? title : "Confirm", body ? body : "", btns, 2,
                confirmBgHex ? confirmBgHex : M3Color::PRIMARY);

    while (s_confirmState.result < 0 && m3ModalIsOpen()) {
        lv_timer_handler();
        launcherDelayMs(5);
    }
    return s_confirmState.result == 1;
}

int launcherUiSelectOption(const char *title,
                           const std::vector<LauncherUiOption> &options,
                           const char *subtitle) {
    if (options.empty()) return -1;

    // Close any previous modal so the option picker sits cleanly on lv_layer_top().
    m3ModalClose();
    s_pickerState.chosen = -2;

    lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(backdrop, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_set_style_pad_all(backdrop, 0, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(backdrop, pickerCancelCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *card = lv_obj_create(backdrop);
    lv_obj_set_size(card, 200, 240);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(m3OutlineHex()), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, title ? title : "Choose Option");
    lv_obj_set_width(titleLbl, lv_pct(100));
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(m3PrimaryHex()), 0);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);

    if (subtitle && subtitle[0]) {
        lv_obj_t *subLbl = lv_label_create(card);
        lv_label_set_text(subLbl, subtitle);
        lv_obj_set_width(subLbl, lv_pct(100));
        lv_label_set_long_mode(subLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(subLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_12, 0);
    }

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for (size_t i = 0; i < options.size(); ++i) {
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(btn, 32, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(m3SurfaceHighestHex()), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 10, 0);
        lv_obj_set_style_pad_ver(btn, 6, 0);
        lv_obj_add_event_cb(btn, pickerRowCb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, options[i].label.c_str());
        lv_obj_set_width(lbl, lv_pct(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    }

    while (s_pickerState.chosen == -2) {
        lv_timer_handler();
        launcherDelayMs(5);
    }

    int chosen = s_pickerState.chosen;
    lv_obj_del(backdrop);
    lv_timer_handler();

    if (chosen >= 0 && static_cast<size_t>(chosen) < options.size()) {
        if (options[chosen].operation) options[chosen].operation();
        return chosen;
    }
    return -1;
}

bool launcherUiReboot() {
    launcherUiSaveState();
    launcherConsoleFlush();
    launcherDelayMs(150);
    ESP.restart();
    return true;
}

#endif // !LAUNCHER_BRIDGE_HOST_TEST
