#include "ClockSync.h"

#include <cassert>
#include <cstdint>
#include <ctime>

#include <CrossPointSettings.h>
#include <HalClock.h>
#include <esp_sntp.h>

namespace {
int rtc_write_calls = 0;
int ntp_init_calls = 0;
bool rtc_results[] = {false, true};
int rtc_result_index = 0;
bool clock_valid = true;
time_t mock_epoch = 2000000000;
int time_calls = 0;
}

HalClock halClock;
CrossPointSettings CrossPointSettings::instance;

bool HalClock::isSystemTimeValid() const { return clock_valid; }

bool HalClock::syncRtcFromSystemTime() {
  ++rtc_write_calls;
  return rtc_results[rtc_result_index++];
}

uint8_t HalClock::getVariantHint() const { return 0; }

void HalClock::configure(const char*, bool, uint8_t, uint32_t) {}

bool CrossPointSettings::saveToFile() const { return true; }

unsigned long millis() { return 100; }

void vTaskDelay(uint32_t) {}

extern "C" time_t time(time_t* result) {
  // Advance on each read so the later call falls strictly inside the
  // twelve-hour throttle window, not within the same wall-clock second.
  const time_t now = mock_epoch + time_calls++;
  if (result != nullptr) *result = now;
  return now;
}

bool esp_sntp_enabled() { return false; }
void esp_sntp_stop() {}
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t) {}
void esp_sntp_setservername(uint8_t, char*) {}
void esp_sntp_init() { ++ntp_init_calls; }
sntp_sync_status_t sntp_get_sync_status() { return SNTP_SYNC_STATUS_COMPLETED; }

int main() {
  // NTP succeeds, but RTC persistence fails. A later ordinary sync must
  // acquire again and retry the RTC write instead of claiming it is recent.
  assert(!ClockSync::syncWithNtp(8000, false));
  assert(rtc_write_calls == 1);
  assert(ntp_init_calls == 1);

  assert(ClockSync::syncWithNtp(8000, false));
  assert(rtc_write_calls == 2);
  assert(ntp_init_calls == 2);
  return 0;
}
