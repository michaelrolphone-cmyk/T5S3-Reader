/* Frozen rtc.clock/API2 fields from Watch 9cfa2aa4 include/twatch_caps.h.
 * Run on host and Xtensa to prevent accidentally inventing another contract. */
#include "RiscRtcClockV2.h"
typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} twatch_rtc_time_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*read)(void *context, twatch_rtc_time_v1 *out);
    bool (*write)(void *context, const twatch_rtc_time_v1 *in);
    bool (*alarm)(void *, uint8_t minute, uint8_t hour, uint8_t day, uint8_t weekday, bool enable);
    bool (*alarm_pending)(void *, bool *pending, bool acknowledge);
} twatch_rtc_api_v1;
#define SAME_FIELD(a,b,f) _Static_assert(offsetof(a,f) == offsetof(b,f), #f " layout changed")
_Static_assert(RISC_RTC_CLOCK_API_V2 == 2, "API version changed");
_Static_assert(sizeof(twatch_rtc_time_v1) == sizeof(risc_rtc_time_v2), "calendar size changed");
_Static_assert(sizeof(twatch_rtc_api_v1) == sizeof(risc_rtc_clock_api_v2), "API size changed");
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,year);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,month);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,day);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,weekday);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,hour);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,minute);
SAME_FIELD(twatch_rtc_time_v1,risc_rtc_time_v2,second);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,api_version);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,struct_size);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,context);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,read);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,write);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,alarm);
SAME_FIELD(twatch_rtc_api_v1,risc_rtc_clock_api_v2,alarm_pending);
