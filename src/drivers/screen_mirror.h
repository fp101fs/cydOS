#pragma once

#include <stdint.h>
#include <stddef.h>

// 3x3 downsampled mirror framebuffer: 80x108 RGB565 (17.2 KB, lazily allocated)
static constexpr uint16_t MIRROR_W          = 80;
static constexpr uint16_t MIRROR_H          = 108;
static constexpr uint16_t MIRROR_BAND_H     = 12;
static constexpr uint8_t  MIRROR_BAND_COUNT = MIRROR_H / MIRROR_BAND_H; // 9 bands

void screenMirrorInit();
void screenMirrorTick(); // Call from main loop (LVGL thread)

// Controls
bool screenMirrorSetStreaming(bool enable);
bool screenMirrorIsStreaming();
void screenMirrorSetRemoteControl(bool enable);
bool screenMirrorIsRemoteControlEnabled();
void screenMirrorForceFullSync();

// Telemetry
uint32_t screenMirrorGetBandsSent();
uint32_t screenMirrorGetRemoteInputsCount();
uint8_t  screenMirrorGetFps();

// Called by companion_mgr worker thread (thread-safe)
void screenMirrorOnCompanionDisconnected();
void screenMirrorEnqueueInput(const char* action, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t durMs);
void screenMirrorEnqueueCmd(const char* cmd, const char* argStr, int32_t argInt);
void screenMirrorRequestUiTree();

// Outbound data popped by companion_mgr worker thread
bool screenMirrorPopPendingJson(char* outBuf, size_t cap);
bool screenMirrorPopDirtyBand(uint8_t* outBuf, size_t cap, size_t* outLen);
