#pragma once

#include <stdint.h>
#include <stddef.h>

static constexpr int16_t TIME_TZ_MIN_LIMIT  = -720;        // UTC-12:00
static constexpr int16_t TIME_TZ_MAX_LIMIT  = 840;         // UTC+14:00
static constexpr int64_t TIME_VALID_EPOCH   = 1704067200;  // 2024-01-01 00:00:00 UTC

enum TimeChangeFlags : uint8_t {
    TIME_CHG_NONE   = 0,
    TIME_CHG_SECOND = 1 << 0,
    TIME_CHG_MINUTE = 1 << 1,
    TIME_CHG_DATE   = 1 << 2,
    TIME_CHG_SYNC   = 1 << 3,
    TIME_CHG_TZ     = 1 << 4,
};

void timeMgrInit();
uint8_t timeMgrTick();

bool timeMgrSyncNow();
void timeMgrSetTzOffsetMin(int16_t offsetMin);
void timeMgrStepTzHours(int8_t deltaHours);
int16_t timeMgrGetTzOffsetMin();
bool timeMgrIsSyncing();

void timeMgrFormatHHMM(char* buf, size_t len);
void timeMgrFormatHHMMSS(char* buf, size_t len);
void timeMgrFormatShortDate(char* buf, size_t len);
void timeMgrFormatLongDate(char* buf, size_t len);
void timeMgrFormatTz(char* buf, size_t len);
