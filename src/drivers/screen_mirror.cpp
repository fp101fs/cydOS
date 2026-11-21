#include "screen_mirror.h"
#include "display_drv.h"
#include "touch_drv.h"
#include "companion_mgr.h"
#include "../theme/m3_theme.h"
#include "../ui/app_manager.h"
#include "../ui/quick_settings.h"
#include "../ui/home_screen.h"
#include "../components/m3_keyboard.h"
#include "../components/m3_modal.h"
#include "../components/m3_widgets.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// Internal State & Synchronization
// ============================================================================

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define SM_LOCK()   portENTER_CRITICAL(&s_mux)
#define SM_UNLOCK() portEXIT_CRITICAL(&s_mux)

static uint16_t* s_fb             = nullptr; // 120 * 160 RGB565 (big-endian, LV_COLOR_16_SWAP=1)
static bool      s_streaming      = false;
static bool      s_remoteEnabled  = true;
static uint16_t  s_dirtyBandMask  = 0;
static uint8_t   s_nextBandCursor = 0;

static uint32_t  s_bandsSent      = 0;
static uint32_t  s_remoteInputs   = 0;
static uint32_t  s_fpsBandsWindow = 0;
static uint32_t  s_fpsWindowMs    = 0;
static uint8_t   s_currentFps     = 0;

// Deferred UI tree sync
static volatile bool s_wantUiTree     = false;
static volatile bool s_wantStatusPush = false;
static uint32_t      s_autoUiTreeAtMs = 0;
static char          s_lastActiveApp[32] = "";

// Outbound JSON slot (single large buffer for ui_tree or mirror_status)
static constexpr size_t SM_JSON_CAP = 3600;
static char* s_outJsonBuf   = nullptr;
static bool  s_outJsonReady = false;

// Inbound command queue (worker -> main LVGL thread)
enum class PendingOpKind : uint8_t {
    NONE = 0,
    TOUCH_IN,
    REMOTE_CMD
};

struct PendingOp {
    PendingOpKind kind;
    char          name[20];
    char          argStr[40];
    int16_t       x1, y1, x2, y2;
    int32_t       argInt;
};

static constexpr size_t OP_QUEUE_CAP = 8;
static PendingOp s_opQ[OP_QUEUE_CAP];
static size_t    s_opHead  = 0;
static size_t    s_opTail  = 0;
static size_t    s_opCount = 0;

static void copyStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

// ============================================================================
// Display Flush Hook (2x2 Subsampling into 120x160 RGB565)
// ============================================================================

static void mirrorFlushHook(const lv_area_t* area, const lv_color_t* color_p) {
    if (!s_streaming || !s_fb || !area || !color_p) return;

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    if (x2 < 0 || y2 < 0) return;

    int32_t srcW = x2 - x1 + 1;
    uint16_t dirtyMask = 0;

    int32_t syStart = (y1 & 1) ? (y1 + 1) : y1;
    int32_t sxStart = (x1 & 1) ? (x1 + 1) : x1;

    for (int32_t sy = syStart; sy <= y2; sy += 2) {
        int32_t dy = sy >> 1;
        if (dy < 0 || dy >= MIRROR_H) continue;

        const lv_color_t* srcRow = color_p + (sy - y1) * srcW;
        uint16_t* dstRow = s_fb + dy * MIRROR_W;

        for (int32_t sx = sxStart; sx <= x2; sx += 2) {
            int32_t dx = sx >> 1;
            if (dx >= 0 && dx < MIRROR_W) {
                dstRow[dx] = srcRow[sx - x1].full;
            }
        }
        uint8_t band = (uint8_t)(dy / MIRROR_BAND_H);
        if (band < MIRROR_BAND_COUNT) {
            dirtyMask |= (uint16_t)(1U << band);
        }
    }

    if (dirtyMask) {
        SM_LOCK();
        s_dirtyBandMask |= dirtyMask;
        SM_UNLOCK();
    }
}

// ============================================================================
// Programmatic LVGL UI Tree Inspector (runs on LVGL main thread)
// ============================================================================

struct UiElem {
    uint8_t  id;
    char     type[12];
    char     label[28];
    int16_t  x, y, w, h;
    bool     checked;
};

static constexpr size_t MAX_UI_ELEMS = 26;

static void findFirstLabelText(lv_obj_t* obj, char* out, size_t cap, uint8_t depth = 0) {
    if (!obj || out[0] || depth > 4) return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char* txt = lv_label_get_text(obj);
        if (txt && txt[0]) {
            // Strip LVGL UTF-8 private-use icon bytes (0xEF 0x80..0x9F ...) for clean text
            size_t oi = 0;
            for (size_t i = 0; txt[i] && oi + 1 < cap; ) {
                uint8_t c = (uint8_t)txt[i];
                if (c == 0xEF && (uint8_t)txt[i + 1] >= 0x80) {
                    i += 3; // Skip 3-byte FontAwesome symbol
                    continue;
                }
                if (c == '\n' || c == '\r' || c == '\t') {
                    if (oi > 0 && out[oi - 1] != ' ') out[oi++] = ' ';
                    i++;
                    continue;
                }
                if (c >= 32 && c < 127) {
                    out[oi++] = (char)c;
                }
                i++;
            }
            while (oi > 0 && out[oi - 1] == ' ') --oi;
            out[oi] = '\0';
            // Trim leading spaces
            size_t lead = 0;
            while (out[lead] == ' ') ++lead;
            if (lead > 0) memmove(out, out + lead, strlen(out + lead) + 1);
            if (out[0]) return;
        }
    }

    uint32_t cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < cnt && !out[0]; ++i) {
        findFirstLabelText(lv_obj_get_child(obj, i), out, cap, depth + 1);
    }
}

static bool isInteractiveWidget(lv_obj_t* obj, const char** outType) {
    if (lv_obj_check_type(obj, &lv_switch_class)) { *outType = "switch"; return true; }
    if (lv_obj_check_type(obj, &lv_slider_class)) { *outType = "slider"; return true; }
    if (lv_obj_check_type(obj, &lv_textarea_class)) { *outType = "textarea"; return true; }
    if (lv_obj_check_type(obj, &lv_btn_class)) { *outType = "btn"; return true; }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        // Ignore full-screen root/backdrop containers
        lv_area_t c;
        lv_obj_get_coords(obj, &c);
        int16_t w = c.x2 - c.x1 + 1;
        int16_t h = c.y2 - c.y1 + 1;
        if (w >= 230 && h >= 220) return false;
        if (w < 14 || h < 14) return false;
        *outType = "card";
        return true;
    }
    return false;
}

static void collectUiTree(lv_obj_t* root, UiElem* elems, size_t& count, uint8_t depth = 0) {
    if (!root || count >= MAX_UI_ELEMS || depth > 7) return;
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;

    lv_area_t coords;
    lv_obj_get_coords(root, &coords);
    int16_t scrW = (int16_t)displayWidth();
    int16_t scrH = (int16_t)displayHeight();
    // Skip off-screen elements
    if (coords.x2 < 0 || coords.y2 < 0 || coords.x1 >= scrW || coords.y1 >= scrH) return;

    const char* wType = nullptr;
    if (depth > 0 && isInteractiveWidget(root, &wType)) {
        int16_t x = coords.x1 < 0 ? 0 : coords.x1;
        int16_t y = coords.y1 < 0 ? 0 : coords.y1;
        int16_t x2 = coords.x2 >= scrW ? (scrW - 1) : coords.x2;
        int16_t y2 = coords.y2 >= scrH ? (scrH - 1) : coords.y2;
        int16_t w = x2 - x + 1;
        int16_t h = y2 - y + 1;

        char lbl[28] = "";
        findFirstLabelText(root, lbl, sizeof(lbl));

        // Only record cards if they have a readable label (avoids noisy containers)
        bool keep = (strcmp(wType, "card") != 0) || (lbl[0] != '\0');
        if (keep && w >= 14 && h >= 14) {
            UiElem& e = elems[count];
            e.id = (uint8_t)count;
            copyStr(e.type, sizeof(e.type), wType);
            copyStr(e.label, sizeof(e.label), lbl);
            e.x = x;
            e.y = y;
            e.w = w;
            e.h = h;
            e.checked = lv_obj_has_state(root, LV_STATE_CHECKED);
            count++;
            // Don't recurse into simple buttons/switches to avoid duplicate child hits
            if (strcmp(wType, "btn") == 0 || strcmp(wType, "switch") == 0) {
                return;
            }
        }
    }

    uint32_t cnt = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < cnt && count < MAX_UI_ELEMS; ++i) {
        collectUiTree(lv_obj_get_child(root, i), elems, count, depth + 1);
    }
}

static bool tapWidgetByLabel(const char* targetLabel) {
    if (!targetLabel || !targetLabel[0]) return false;
    UiElem elems[MAX_UI_ELEMS];
    size_t count = 0;
    collectUiTree(lv_layer_top(), elems, count);
    collectUiTree(lv_scr_act(), elems, count);

    for (size_t i = 0; i < count; ++i) {
        if (elems[i].label[0] && strcasecmp(elems[i].label, targetLabel) == 0) {
            int16_t cx = (int16_t)(elems[i].x + elems[i].w / 2);
            int16_t cy = (int16_t)(elems[i].y + elems[i].h / 2);
            touchInjectTap(cx, cy);
            return true;
        }
    }
    // Fallback: partial case-insensitive match
    for (size_t i = 0; i < count; ++i) {
        if (elems[i].label[0]) {
            String hay(elems[i].label);
            String ned(targetLabel);
            hay.toLowerCase();
            ned.toLowerCase();
            if (hay.indexOf(ned) >= 0) {
                int16_t cx = (int16_t)(elems[i].x + elems[i].w / 2);
                int16_t cy = (int16_t)(elems[i].y + elems[i].h / 2);
                touchInjectTap(cx, cy);
                return true;
            }
        }
    }
    return false;
}

static void publishJson(const char* json) {
    if (!json || !json[0]) return;
    if (!s_outJsonBuf) {
        s_outJsonBuf = (char*)malloc(SM_JSON_CAP);
        if (!s_outJsonBuf) return;
    }
    SM_LOCK();
    copyStr(s_outJsonBuf, SM_JSON_CAP, json);
    s_outJsonReady = true;
    SM_UNLOCK();
}

static void buildAndPublishUiTree() {
    UiElem elems[MAX_UI_ELEMS];
    size_t count = 0;
    collectUiTree(lv_layer_top(), elems, count);
    collectUiTree(lv_scr_act(), elems, count);

    JsonDocument doc;
    doc["v"] = COMPANION_PROTOCOL_VER;
    doc["type"] = "ui_tree";
    doc["w"] = displayWidth();
    doc["h"] = displayHeight();
    doc["active_app"] = appManagerGetActiveId();
    doc["active_name"] = appManagerGetActiveName();
    doc["streaming"] = s_streaming;
    doc["remote_enabled"] = s_remoteEnabled;
    doc["keyboard_open"] = m3KeyboardIsOpen();
    doc["quick_settings_open"] = quickSettingsIsOpen();

    JsonArray arr = doc["elements"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = elems[i].id;
        o["type"] = elems[i].type;
        if (elems[i].label[0]) o["label"] = elems[i].label;
        o["x"] = elems[i].x;
        o["y"] = elems[i].y;
        o["w"] = elems[i].w;
        o["h"] = elems[i].h;
        o["cx"] = elems[i].x + elems[i].w / 2;
        o["cy"] = elems[i].y + elems[i].h / 2;
        if (elems[i].checked) o["checked"] = true;
    }

    if (!s_outJsonBuf) {
        s_outJsonBuf = (char*)malloc(SM_JSON_CAP);
        if (!s_outJsonBuf) return;
    }
    char* tmp = (char*)malloc(SM_JSON_CAP);
    if (!tmp) return;
    size_t n = serializeJson(doc, tmp, SM_JSON_CAP);
    if (n > 0 && n < SM_JSON_CAP) {
        publishJson(tmp);
    }
    free(tmp);
}

static void publishMirrorStatus() {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"v\":%u,\"type\":\"mirror_status\",\"streaming\":%s,\"remote_enabled\":%s,\"w\":%u,\"h\":%u,\"active_app\":\"%s\"}",
             (unsigned)COMPANION_PROTOCOL_VER,
             s_streaming ? "true" : "false",
             s_remoteEnabled ? "true" : "false",
             (unsigned)MIRROR_W,
             (unsigned)MIRROR_H,
             appManagerGetActiveId());
    publishJson(buf);
}

// ============================================================================
// Lifecycle & Controls
// ============================================================================

void screenMirrorInit() {
    s_fb = nullptr;
    s_streaming = false;
    s_remoteEnabled = true;
    s_dirtyBandMask = 0;
    s_nextBandCursor = 0;
    s_bandsSent = 0;
    s_remoteInputs = 0;
    s_fpsBandsWindow = 0;
    s_fpsWindowMs = millis();
    s_currentFps = 0;
    s_wantUiTree = false;
    s_wantStatusPush = false;
    s_autoUiTreeAtMs = 0;
    copyStr(s_lastActiveApp, sizeof(s_lastActiveApp), "home");
}

bool screenMirrorSetStreaming(bool enable) {
    if (enable == s_streaming) {
        if (enable) screenMirrorForceFullSync();
        return true;
    }

    if (enable) {
        if (!s_fb) {
            s_fb = (uint16_t*)calloc(MIRROR_W * MIRROR_H, sizeof(uint16_t));
            if (!s_fb) {
                Serial.println("[MIRROR] OOM allocating 120x160 framebuffer");
                return false;
            }
        }
        s_streaming = true;
        displaySetFlushHook(mirrorFlushHook);
        screenMirrorForceFullSync();
        s_wantUiTree = true;
        Serial.printf("[MIRROR] Streaming ON (Free Heap: %lu B)\n", (unsigned long)ESP.getFreeHeap());
    } else {
        s_streaming = false;
        displaySetFlushHook(nullptr);
        SM_LOCK();
        s_dirtyBandMask = 0;
        SM_UNLOCK();
        if (s_fb) {
            free(s_fb);
            s_fb = nullptr;
        }
        s_currentFps = 0;
        s_wantStatusPush = true;
        Serial.printf("[MIRROR] Streaming OFF (Free Heap: %lu B)\n", (unsigned long)ESP.getFreeHeap());
    }
    return true;
}

bool screenMirrorIsStreaming() { return s_streaming; }

void screenMirrorSetRemoteControl(bool enable) {
    s_remoteEnabled = enable;
    s_wantStatusPush = true;
}

bool screenMirrorIsRemoteControlEnabled() { return s_remoteEnabled; }

void screenMirrorForceFullSync() {
    if (!s_streaming) return;
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    SM_LOCK();
    s_dirtyBandMask = (uint16_t)((1U << MIRROR_BAND_COUNT) - 1U);
    SM_UNLOCK();
    s_wantUiTree = true;
}

uint32_t screenMirrorGetBandsSent() { return s_bandsSent; }
uint32_t screenMirrorGetRemoteInputsCount() { return s_remoteInputs; }
uint8_t  screenMirrorGetFps() { return s_currentFps; }

void screenMirrorOnCompanionDisconnected() {
    if (s_streaming) {
        // Request stop on main loop by clearing streaming flag safely
        s_streaming = false;
        displaySetFlushHook(nullptr);
        SM_LOCK();
        s_dirtyBandMask = 0;
        SM_UNLOCK();
    }
}

// ============================================================================
// Thread-Safe Enqueue (called from companion_mgr worker thread)
// ============================================================================

static void pushOp(const PendingOp& op) {
    SM_LOCK();
    if (s_opCount < OP_QUEUE_CAP) {
        s_opQ[s_opTail] = op;
        s_opTail = (s_opTail + 1) % OP_QUEUE_CAP;
        s_opCount++;
    }
    SM_UNLOCK();
}

void screenMirrorEnqueueInput(const char* action, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t durMs) {
    PendingOp op;
    memset(&op, 0, sizeof(op));
    op.kind = PendingOpKind::TOUCH_IN;
    copyStr(op.name, sizeof(op.name), action ? action : "");
    op.x1 = x1; op.y1 = y1;
    op.x2 = x2; op.y2 = y2;
    op.argInt = durMs;
    pushOp(op);
}

void screenMirrorEnqueueCmd(const char* cmd, const char* argStr, int32_t argInt) {
    PendingOp op;
    memset(&op, 0, sizeof(op));
    op.kind = PendingOpKind::REMOTE_CMD;
    copyStr(op.name, sizeof(op.name), cmd ? cmd : "");
    copyStr(op.argStr, sizeof(op.argStr), argStr ? argStr : "");
    op.argInt = argInt;
    pushOp(op);
}

void screenMirrorRequestUiTree() {
    s_wantUiTree = true;
}

// ============================================================================
// Main Loop Execution (LVGL thread)
// ============================================================================

static void executePendingOp(const PendingOp& op) {
    if (op.kind == PendingOpKind::TOUCH_IN) {
        if (!s_remoteEnabled) return;
        s_remoteInputs++;
        if (strcmp(op.name, "tap") == 0) {
            touchInjectTap(op.x1, op.y1);
            s_autoUiTreeAtMs = millis() + 260;
        } else if (strcmp(op.name, "down") == 0 || strcmp(op.name, "move") == 0) {
            touchInjectPointer(true, op.x1, op.y1);
        } else if (strcmp(op.name, "up") == 0) {
            touchInjectPointer(false, op.x1, op.y1);
            s_autoUiTreeAtMs = millis() + 260;
        } else if (strcmp(op.name, "swipe") == 0) {
            touchInjectSwipe(op.x1, op.y1, op.x2, op.y2, (uint16_t)(op.argInt > 0 ? op.argInt : 220));
            s_autoUiTreeAtMs = millis() + 360;
        }
        return;
    }

    if (op.kind == PendingOpKind::REMOTE_CMD) {
        if (strcmp(op.name, "stream_on") == 0) {
            screenMirrorSetStreaming(true);
            return;
        }
        if (strcmp(op.name, "stream_off") == 0) {
            screenMirrorSetStreaming(false);
            return;
        }
        if (strcmp(op.name, "refresh_ui") == 0) {
            screenMirrorForceFullSync();
            return;
        }
        if (!s_remoteEnabled) return;
        s_remoteInputs++;

        if (strcmp(op.name, "home") == 0) {
            if (m3ModalIsOpen()) m3ModalClose();
            if (m3KeyboardIsOpen()) m3KeyboardHide();
            if (quickSettingsIsOpen()) quickSettingsClose();
            if (appManagerHasActiveApp()) appManagerClose();
            s_autoUiTreeAtMs = millis() + 260;
        } else if (strcmp(op.name, "back") == 0) {
            if (m3ModalIsOpen()) m3ModalClose();
            else if (m3KeyboardIsOpen()) m3KeyboardHide();
            else if (quickSettingsIsOpen()) quickSettingsClose();
            else if (appManagerHasActiveApp()) appManagerClose();
            s_autoUiTreeAtMs = millis() + 260;
        } else if (strcmp(op.name, "quick_settings") == 0) {
            if (quickSettingsIsOpen()) quickSettingsClose();
            else quickSettingsOpen();
            s_autoUiTreeAtMs = millis() + 260;
        } else if (strcmp(op.name, "launch_app") == 0 && op.argStr[0]) {
            if (quickSettingsIsOpen()) quickSettingsClose();
            appManagerLaunchById(op.argStr);
            s_autoUiTreeAtMs = millis() + 300;
        } else if (strcmp(op.name, "tap_label") == 0 && op.argStr[0]) {
            tapWidgetByLabel(op.argStr);
            s_autoUiTreeAtMs = millis() + 280;
        } else if (strcmp(op.name, "brightness") == 0) {
            displaySetBrightness((uint8_t)op.argInt);
        }
    }
}

void screenMirrorTick() {
    // Free framebuffer on main thread if disconnected turned streaming off
    if (!s_streaming && s_fb) {
        free(s_fb);
        s_fb = nullptr;
    }

    // Drain pending remote operations on LVGL thread
    for (int i = 0; i < 4; ++i) {
        PendingOp op;
        bool hasOp = false;
        SM_LOCK();
        if (s_opCount > 0) {
            op = s_opQ[s_opHead];
            s_opHead = (s_opHead + 1) % OP_QUEUE_CAP;
            s_opCount--;
            hasOp = true;
        }
        SM_UNLOCK();
        if (!hasOp) break;
        executePendingOp(op);
    }

    // Detect active app changes while streaming to keep Android's UI tree fresh
    const char* curApp = appManagerGetActiveId();
    if (s_streaming && strcmp(curApp, s_lastActiveApp) != 0) {
        copyStr(s_lastActiveApp, sizeof(s_lastActiveApp), curApp);
        s_autoUiTreeAtMs = millis() + 260;
    }

    uint32_t now = millis();
    if (s_autoUiTreeAtMs != 0 && (int32_t)(now - s_autoUiTreeAtMs) >= 0) {
        s_autoUiTreeAtMs = 0;
        if (s_streaming) s_wantUiTree = true;
    }

    if (s_wantUiTree && !s_outJsonReady) {
        s_wantUiTree = false;
        buildAndPublishUiTree();
    } else if (s_wantStatusPush && !s_outJsonReady) {
        s_wantStatusPush = false;
        publishMirrorStatus();
    }

    // Update FPS estimate once per second (10 bands = 1 full frame)
    if ((uint32_t)(now - s_fpsWindowMs) >= 1000) {
        uint32_t bands = s_fpsBandsWindow;
        s_fpsBandsWindow = 0;
        s_fpsWindowMs = now;
        s_currentFps = (uint8_t)((bands + 5) / MIRROR_BAND_COUNT);
    }
}

// ============================================================================
// Outbound Pop Functions (called by companion_mgr worker thread)
// ============================================================================

bool screenMirrorPopPendingJson(char* outBuf, size_t cap) {
    if (!outBuf || cap == 0 || !s_outJsonBuf) return false;
    bool got = false;
    SM_LOCK();
    if (s_outJsonReady) {
        copyStr(outBuf, cap, s_outJsonBuf);
        s_outJsonReady = false;
        got = true;
    }
    SM_UNLOCK();
    return got;
}

// PackBits-style 16-bit RGB565 RLE encoder for one 120x16 band.
// Wire format (10-byte header + RLE payload):
//   [0]='C', [1]='Y', [2]=0x01 (RLE RGB565 BE), [3]=bandIdx
//   [4..5]=width (LE), [6..7]=yOffset (LE), [8..9]=bandHeight (LE)
//   Payload: control byte c:
//     if (c & 0x80): repeat next 2-byte pixel ((c & 0x7F) + 1) times
//     else:          copy (c + 1) literal 2-byte pixels
bool screenMirrorPopDirtyBand(uint8_t* outBuf, size_t cap, size_t* outLen) {
    if (!s_streaming || !s_fb || !outBuf || cap < 64 || !outLen) return false;

    int foundBand = -1;
    SM_LOCK();
    if (s_dirtyBandMask != 0) {
        for (uint8_t step = 0; step < MIRROR_BAND_COUNT; ++step) {
            uint8_t idx = (uint8_t)((s_nextBandCursor + step) % MIRROR_BAND_COUNT);
            if (s_dirtyBandMask & (1U << idx)) {
                s_dirtyBandMask &= (uint16_t)~(1U << idx);
                s_nextBandCursor = (uint8_t)((idx + 1) % MIRROR_BAND_COUNT);
                foundBand = idx;
                break;
            }
        }
    }
    SM_UNLOCK();

    if (foundBand < 0) return false;

    const uint16_t yOff = (uint16_t)(foundBand * MIRROR_BAND_H);
    const uint16_t* src = s_fb + (size_t)yOff * MIRROR_W;
    const size_t totalPx = (size_t)MIRROR_W * MIRROR_BAND_H;

    // 10-byte header
    size_t pos = 0;
    outBuf[pos++] = 'C';
    outBuf[pos++] = 'Y';
    outBuf[pos++] = 0x01;
    outBuf[pos++] = (uint8_t)foundBand;
    outBuf[pos++] = (uint8_t)(MIRROR_W & 0xFF);
    outBuf[pos++] = (uint8_t)((MIRROR_W >> 8) & 0xFF);
    outBuf[pos++] = (uint8_t)(yOff & 0xFF);
    outBuf[pos++] = (uint8_t)((yOff >> 8) & 0xFF);
    outBuf[pos++] = (uint8_t)(MIRROR_BAND_H & 0xFF);
    outBuf[pos++] = (uint8_t)((MIRROR_BAND_H >> 8) & 0xFF);

    size_t i = 0;
    while (i < totalPx) {
        // Check for a run of >= 2 identical pixels
        size_t runLen = 1;
        uint16_t first = src[i];
        while (i + runLen < totalPx && runLen < 128 && src[i + runLen] == first) {
            runLen++;
        }

        if (runLen >= 2) {
            if (pos + 3 > cap) return false;
            outBuf[pos++] = (uint8_t)(0x80 | (runLen - 1));
            outBuf[pos++] = (uint8_t)(first & 0xFF);
            outBuf[pos++] = (uint8_t)((first >> 8) & 0xFF);
            i += runLen;
        } else {
            // Literal run until a repeat of >= 2 pixels or 128 pixels
            size_t litStart = i;
            size_t litLen = 1;
            while (litStart + litLen < totalPx && litLen < 128) {
                if (litStart + litLen + 1 < totalPx &&
                    src[litStart + litLen] == src[litStart + litLen + 1]) {
                    break;
                }
                litLen++;
            }
            if (pos + 1 + litLen * 2 > cap) return false;
            outBuf[pos++] = (uint8_t)(litLen - 1);
            for (size_t k = 0; k < litLen; ++k) {
                uint16_t px = src[litStart + k];
                outBuf[pos++] = (uint8_t)(px & 0xFF);
                outBuf[pos++] = (uint8_t)((px >> 8) & 0xFF);
            }
            i += litLen;
        }
    }

    *outLen = pos;
    s_bandsSent++;
    s_fpsBandsWindow++;
    return true;
}
