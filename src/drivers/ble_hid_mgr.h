#pragma once

#include <stdint.h>
#include <stddef.h>

// Standard Mouse button bitmask
static constexpr uint8_t BLE_MOUSE_LEFT   = 0x01;
static constexpr uint8_t BLE_MOUSE_RIGHT  = 0x02;
static constexpr uint8_t BLE_MOUSE_MIDDLE = 0x04;

// Standard USB HID Consumer Control usages (Media Remote / Fire TV)
static constexpr uint16_t HID_CC_POWER      = 0x0030;
static constexpr uint16_t HID_CC_MENU       = 0x0040;
static constexpr uint16_t HID_CC_PLAY_PAUSE = 0x00CD;
static constexpr uint16_t HID_CC_FAST_FWD   = 0x00B3;
static constexpr uint16_t HID_CC_REWIND     = 0x00B4;
static constexpr uint16_t HID_CC_MUTE       = 0x00E2;
static constexpr uint16_t HID_CC_VOL_UP     = 0x00E9;
static constexpr uint16_t HID_CC_VOL_DOWN   = 0x00EA;
static constexpr uint16_t HID_CC_AC_HOME    = 0x0223;
static constexpr uint16_t HID_CC_AC_BACK    = 0x0224;

// Standard USB HID Keyboard usages (D-pad / Fire TV navigation)
static constexpr uint8_t HID_KEY_ENTER      = 0x28;
static constexpr uint8_t HID_KEY_ESC        = 0x29;
static constexpr uint8_t HID_KEY_RIGHT      = 0x4F;
static constexpr uint8_t HID_KEY_LEFT       = 0x50;
static constexpr uint8_t HID_KEY_DOWN       = 0x51;
static constexpr uint8_t HID_KEY_UP         = 0x52;
static constexpr uint8_t HID_KEY_APP_MENU   = 0x65;

bool bleHidStart();
void bleHidStop();
bool bleHidIsActive();
bool bleHidIsConnected();
const char* bleHidStatusText();

void bleHidMouseMove(int8_t dx, int8_t dy, int8_t wheel = 0);
void bleHidMouseSetButtons(uint8_t buttonsMask);
void bleHidMouseClick(uint8_t buttonMask);

void bleHidConsumerTap(uint16_t usageId);
void bleHidKeyTap(uint8_t hidKey, uint8_t modifiers = 0);
