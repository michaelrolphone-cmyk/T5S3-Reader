#pragma once
/* Promoted spelling of the existing Watch rtc.clock/API2 contract, byte-for-byte
 * layout unchanged. Source: RiscRTE-T-Watch-S3 9cfa2aa4d572a290b41cf040a27cbec9d78bb35c
 * include/twatch_caps.h. This does not change the Watch package or its ABI.
 * Calendar fields have no implicit timezone: the consumer owns UTC/local policy.
 * read returns false for invalid/unreliable time. 255 masks an alarm field.
 * Alarm wake support depends on the provider's verified IRQ configuration.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RTC_CLOCK_API_V2 2u
#define RISC_RTC_CLOCK_CAPABILITY "rtc.clock"
typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} risc_rtc_time_v2;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*read)(void *context, risc_rtc_time_v2 *out);
    bool (*write)(void *context, const risc_rtc_time_v2 *in);
    bool (*alarm)(void *, uint8_t minute, uint8_t hour, uint8_t day, uint8_t weekday, bool enable);
    bool (*alarm_pending)(void *, bool *pending, bool acknowledge);
} risc_rtc_clock_api_v2;

#ifdef __cplusplus
}
#endif
