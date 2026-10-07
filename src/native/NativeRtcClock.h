#pragma once
#include <RiscRtcClockV2.h>

// Firmware consumer of the existing calendar capability. Call begin on the
// invocation-owner task after package admission. Render/network worker tasks
// cannot acquire/use/release provider leases through this adapter.
bool nativeRtcBegin();
bool nativeRtcRead(risc_rtc_time_v2* out);
bool nativeRtcWrite(const risc_rtc_time_v2* value);
// No RTC lease normally persists across a call. An uncertain release retains
// its token; suspend retries it and blocks deep sleep if cleanup still fails.
bool nativeRtcSuspend();
bool nativeRtcResume();
