#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace M3Color {
    constexpr uint32_t BG_DARK         = 0x0F1117;
    constexpr uint32_t BG_AMOLED       = 0x000000;
    constexpr uint32_t SURFACE         = 0x191C24;
    constexpr uint32_t SURFACE_HIGH    = 0x222631;
    constexpr uint32_t SURFACE_HIGHEST = 0x2C313F;
    constexpr uint32_t OUTLINE         = 0x353B4B;

    constexpr uint32_t PRIMARY         = 0x8AB4F8;
    constexpr uint32_t PRIMARY_CONT    = 0x1D3557;
    constexpr uint32_t ON_PRIMARY      = 0x07234B;

    constexpr uint32_t SECONDARY       = 0xC5B4E3;
    constexpr uint32_t SECONDARY_CONT  = 0x3A2F52;

    constexpr uint32_t TERTIARY        = 0x81C995;
    constexpr uint32_t TERTIARY_CONT   = 0x1B4332;

    constexpr uint32_t CYAN            = 0x78D9EC;
    constexpr uint32_t CYAN_CONT       = 0x173F49;

    constexpr uint32_t AMBER           = 0xFDD663;
    constexpr uint32_t CORAL           = 0xF28B82;

    constexpr uint32_t TEXT_PRIMARY    = 0xE8EAED;
    constexpr uint32_t TEXT_SECONDARY  = 0x9AA0A6;
    constexpr uint32_t TEXT_MUTED      = 0x6E747E;
}

namespace M3Dim {
    constexpr int16_t STATUS_BAR_H = 24;
    constexpr int16_t NAV_BAR_H    = 38;
    constexpr int16_t APP_TOP_H    = 40;
    constexpr int16_t PAD_SM       = 6;
    constexpr int16_t PAD_MD       = 10;
    constexpr int16_t PAD_LG       = 14;
    constexpr int16_t RADIUS_CARD  = 16;
    constexpr int16_t RADIUS_TILE  = 14;
    constexpr int16_t RADIUS_PILL  = 999;
}

struct SystemState {
    bool wifiEnabled    = true;
    bool wifiConnected  = false;
    bool wifiConnecting = false;
    char wifiSsid[33]   = "";
    char wifiIp[16]     = "";
    int8_t wifiRssi     = 0;
    int8_t wifiBars     = 0;

    bool btEnabled      = true;
    bool btScanning     = false;
    uint8_t btFoundCount = 0;
    char btDevice[32]   = "";

    bool airplaneMode   = false;
    bool flashlightOn   = false;
    bool lightMode      = false;
    bool amoledBg       = false;
    bool ledFeedback    = true;
    bool autoLock       = false;

    uint8_t batteryPct  = 88;
    bool timeSynced     = false;
    uint16_t year       = 2026;
    uint8_t month       = 1;
    uint8_t day         = 1;
    uint8_t weekday     = 4;
    uint8_t hour        = 0;
    uint8_t minute      = 0;
    uint8_t second      = 0;
    int16_t tzOffsetMin = 0;
};

extern SystemState g_sys;

void m3ThemeInit();
uint32_t m3CurrentBgHex();
uint32_t m3SurfaceHex();
uint32_t m3SurfaceHighHex();
uint32_t m3SurfaceHighestHex();
uint32_t m3OutlineHex();
uint32_t m3TextPrimaryHex();
uint32_t m3TextSecondaryHex();
uint32_t m3PrimaryHex();

void m3ApplyThemeAll();
void m3PulseFeedback();
void m3SyncSysTick();
