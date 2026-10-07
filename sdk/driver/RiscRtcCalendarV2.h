#pragma once
/* Reused from Watch include/twatch_calendar.h at 9cfa2aa4. */
#include "RiscRtcClockV2.h"
static inline bool risc_rtc_valid_time_v2(const risc_rtc_time_v2 *t) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!t || t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 || t->weekday > 6 ||
        t->hour > 23 || t->minute > 59 || t->second > 59)
        return false;
    return t->day >= 1 && t->day <= days[t->month - 1] + (t->month == 2 && t->year % 4 == 0);
}
static inline bool risc_rtc_valid_bcd(uint8_t x) {
    return (x & 15) < 10 && (x >> 4) < 10;
}
