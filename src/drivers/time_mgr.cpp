#include "time_mgr.h"
#include "../theme/m3_theme.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <stdio.h>

static constexpr const char* TIME_NVS_NS       = "time";
static constexpr const char* NTP_SERVER_1      = "pool.ntp.org";
static constexpr const char* NTP_SERVER_2      = "time.nist.gov";
static constexpr const char* NTP_SERVER_3      = "time.google.com";
static constexpr uint32_t TIME_POLL_INTERVAL_MS = 250;

static const char* const kDaysShort[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char* const kDaysLong[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char* const kMonthsShort[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char* const kMonthsLong[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static int16_t s_tzOffsetMin   = 0;
static bool s_ntpRequested     = false;
static bool s_syncing          = false;
static uint32_t s_lastPollMs   = 0;
static uint8_t s_pendingFlags  = TIME_CHG_NONE;

static int16_t clampTz(int16_t offsetMin) {
    if (offsetMin < TIME_TZ_MIN_LIMIT) return TIME_TZ_MIN_LIMIT;
    if (offsetMin > TIME_TZ_MAX_LIMIT) return TIME_TZ_MAX_LIMIT;
    return offsetMin;
}

static void saveTzNvs(int16_t offsetMin) {
    Preferences prefs;
    if (prefs.begin(TIME_NVS_NS, false)) {
        prefs.putShort("tz_min", offsetMin);
        prefs.end();
    }
}

static void startNtp() {
    configTime((long)s_tzOffsetMin * 60L, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    Serial.printf("[TIME] configTime(offset=%d min) -> %s\n", (int)s_tzOffsetMin, NTP_SERVER_1);
}

static uint8_t updateFromEpoch(time_t now) {
    if ((int64_t)now < TIME_VALID_EPOCH) {
        return TIME_CHG_NONE;
    }

    time_t localNow = now + (time_t)s_tzOffsetMin * 60;
    struct tm tmBuf;
    gmtime_r(&localNow, &tmBuf);

    uint16_t newYear   = (uint16_t)(tmBuf.tm_year + 1900);
    uint8_t newMonth   = (uint8_t)(tmBuf.tm_mon + 1);
    uint8_t newDay     = (uint8_t)tmBuf.tm_mday;
    uint8_t newWeekday = (uint8_t)(tmBuf.tm_wday % 7);
    uint8_t newHour    = (uint8_t)tmBuf.tm_hour;
    uint8_t newMinute  = (uint8_t)tmBuf.tm_min;
    uint8_t newSecond  = (uint8_t)tmBuf.tm_sec;

    uint8_t flags = TIME_CHG_NONE;
    if (!g_sys.timeSynced) {
        g_sys.timeSynced = true;
        flags |= TIME_CHG_SYNC | TIME_CHG_DATE | TIME_CHG_MINUTE | TIME_CHG_SECOND;
        Serial.printf("[TIME] NTP synced: %04u-%02u-%02u %02u:%02u:%02u\n",
                      (unsigned)newYear, (unsigned)newMonth, (unsigned)newDay,
                      (unsigned)newHour, (unsigned)newMinute, (unsigned)newSecond);
    }
    if (s_syncing) {
        s_syncing = false;
        flags |= TIME_CHG_SYNC;
    }
    if (newYear != g_sys.year || newMonth != g_sys.month ||
        newDay != g_sys.day || newWeekday != g_sys.weekday) {
        flags |= TIME_CHG_DATE;
    }
    if (newHour != g_sys.hour || newMinute != g_sys.minute) {
        flags |= TIME_CHG_MINUTE;
    }
    if (newSecond != g_sys.second) {
        flags |= TIME_CHG_SECOND;
    }

    g_sys.year    = newYear;
    g_sys.month   = newMonth;
    g_sys.day     = newDay;
    g_sys.weekday = newWeekday;
    g_sys.hour    = newHour;
    g_sys.minute  = newMinute;
    g_sys.second  = newSecond;

    return flags;
}

void timeMgrInit() {
    s_tzOffsetMin  = 0;
    s_ntpRequested = false;
    s_syncing      = false;
    s_lastPollMs   = 0;
    s_pendingFlags = TIME_CHG_NONE;

    Preferences prefs;
    if (prefs.begin(TIME_NVS_NS, true)) {
        s_tzOffsetMin = clampTz(prefs.getShort("tz_min", 0));
        prefs.end();
    }

    g_sys.tzOffsetMin = s_tzOffsetMin;
    g_sys.timeSynced  = false;
    g_sys.year        = 2026;
    g_sys.month       = 1;
    g_sys.day         = 1;
    g_sys.weekday     = 4;
    g_sys.hour        = 0;
    g_sys.minute      = 0;
    g_sys.second      = 0;

    // If RTC already has valid epoch (e.g. soft reset), adopt it immediately
    updateFromEpoch(time(nullptr));

    if (g_sys.wifiConnected) {
        startNtp();
        s_ntpRequested = true;
        s_syncing = !g_sys.timeSynced;
    }

    Serial.printf("[TIME] Init tz=%d min, synced=%d\n",
                  (int)s_tzOffsetMin, g_sys.timeSynced ? 1 : 0);
}

uint8_t timeMgrTick() {
    uint8_t flags = s_pendingFlags;
    s_pendingFlags = TIME_CHG_NONE;

    if (g_sys.wifiConnected) {
        if (!s_ntpRequested) {
            startNtp();
            s_ntpRequested = true;
            if (!g_sys.timeSynced) {
                s_syncing = true;
                flags |= TIME_CHG_SYNC;
            }
        }
    } else {
        if (s_ntpRequested) {
            s_ntpRequested = false;
        }
        if (s_syncing) {
            s_syncing = false;
            flags |= TIME_CHG_SYNC;
        }
    }

    uint32_t nowMs = millis();
    if (s_lastPollMs == 0 || (nowMs - s_lastPollMs >= TIME_POLL_INTERVAL_MS)) {
        s_lastPollMs = nowMs;
        flags |= updateFromEpoch(time(nullptr));
    }

    return flags;
}

bool timeMgrSyncNow() {
    if (!g_sys.wifiConnected) {
        return false;
    }
    startNtp();
    s_ntpRequested = true;
    if (!g_sys.timeSynced) {
        s_syncing = true;
        s_pendingFlags |= TIME_CHG_SYNC;
    }
    return true;
}

void timeMgrSetTzOffsetMin(int16_t offsetMin) {
    int16_t clamped = clampTz(offsetMin);
    if (clamped == s_tzOffsetMin) return;

    s_tzOffsetMin = clamped;
    g_sys.tzOffsetMin = clamped;
    saveTzNvs(clamped);

    uint8_t chg = TIME_CHG_TZ;
    if (g_sys.wifiConnected) {
        startNtp();
        s_ntpRequested = true;
    }
    chg |= updateFromEpoch(time(nullptr));
    s_pendingFlags |= chg;
}

void timeMgrStepTzHours(int8_t deltaHours) {
    int16_t next = (int16_t)(s_tzOffsetMin + (int16_t)deltaHours * 60);
    timeMgrSetTzOffsetMin(next);
}

int16_t timeMgrGetTzOffsetMin() {
    return s_tzOffsetMin;
}

bool timeMgrIsSyncing() {
    return s_syncing;
}

void timeMgrFormatHHMM(char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (!g_sys.timeSynced) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02u:%02u", (unsigned)g_sys.hour, (unsigned)g_sys.minute);
}

void timeMgrFormatHHMMSS(char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (!g_sys.timeSynced) {
        snprintf(buf, len, "--:--:--");
        return;
    }
    snprintf(buf, len, "%02u:%02u:%02u",
             (unsigned)g_sys.hour, (unsigned)g_sys.minute, (unsigned)g_sys.second);
}

void timeMgrFormatShortDate(char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (!g_sys.timeSynced) {
        snprintf(buf, len, "%s", s_syncing ? "Syncing..." : "Not synced");
        return;
    }
    const char* wd = (g_sys.weekday < 7) ? kDaysShort[g_sys.weekday] : "---";
    const char* mo = (g_sys.month >= 1 && g_sys.month <= 12) ? kMonthsShort[g_sys.month - 1] : "---";
    snprintf(buf, len, "%s, %s %u", wd, mo, (unsigned)g_sys.day);
}

void timeMgrFormatLongDate(char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (!g_sys.timeSynced) {
        snprintf(buf, len, "%s",
                 g_sys.wifiConnected ? "Syncing NTP time..." : "Connect Wi-Fi for NTP");
        return;
    }
    const char* wd = (g_sys.weekday < 7) ? kDaysLong[g_sys.weekday] : "Unknown";
    const char* mo = (g_sys.month >= 1 && g_sys.month <= 12) ? kMonthsLong[g_sys.month - 1] : "Unknown";
    snprintf(buf, len, "%s, %s %u, %04u", wd, mo, (unsigned)g_sys.day, (unsigned)g_sys.year);
}

void timeMgrFormatTz(char* buf, size_t len) {
    if (!buf || len == 0) return;
    int16_t m = s_tzOffsetMin;
    char sign = (m >= 0) ? '+' : '-';
    int16_t absM = (m >= 0) ? m : (int16_t)(-m);
    int16_t hrs = absM / 60;
    int16_t rem = absM % 60;
    if (rem == 0) {
        snprintf(buf, len, "UTC%c%d", sign, (int)hrs);
    } else {
        snprintf(buf, len, "UTC%c%d:%02d", sign, (int)hrs, (int)rem);
    }
}
