#include "ClockSync.h"

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <initializer_list>

#include <CrossPointSettings.h>
#include <HalClock.h>
#include <esp_sntp.h>

namespace {
unsigned long ticks;
bool wrap32;
uint32_t spent;
unsigned long attempt_start;
uint32_t complete_after[3];
uint32_t attempt_budget_used[3];
const char* servers[3];
int starts, stops, writes, delays;
bool active, valid, rtc_ok;
time_t epoch = 2000000000;

void reset(unsigned long start, bool target32 = true) {
  assert(!active);
  ticks = start;
  wrap32 = target32;
  spent = 0;
  starts = stops = writes = delays = 0;
  valid = rtc_ok = true;
  for (int i = 0; i < 3; ++i) {
    complete_after[i] = UINT32_MAX;
    attempt_budget_used[i] = 0;
    servers[i] = nullptr;
  }
}

void check_stopped() {
  assert(!active);
  assert(stops == starts);
  ClockSync::stop();
  ClockSync::stop();
  assert(stops == starts);
}
}

HalClock halClock;
CrossPointSettings CrossPointSettings::instance;
bool HalClock::isSystemTimeValid() const { return valid; }
bool HalClock::syncRtcFromSystemTime() { ++writes; return rtc_ok; }
uint8_t HalClock::getVariantHint() const { return 0; }
void HalClock::configure(const char*, bool, uint8_t, uint32_t) {}
bool CrossPointSettings::saveToFile() const { return true; }
unsigned long millis() { return ticks; }
void vTaskDelay(uint32_t amount) {
  assert(active && amount == 100 && ++delays <= 100);
  ticks += amount;
  if (wrap32) ticks = static_cast<uint32_t>(ticks);
  spent += amount;
  attempt_budget_used[starts - 1] += amount;
}
extern "C" time_t time(time_t* result) {
  const time_t now = epoch++;
  if (result) *result = now;
  return now;
}
bool esp_sntp_enabled() { return active; }
void esp_sntp_stop() { assert(active); active = false; ++stops; }
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t mode) {
  assert(!active && mode == ESP_SNTP_OPMODE_POLL);
}
void esp_sntp_setservername(uint8_t index, char* server) {
  assert(!active && index == 0 && starts < 3);
  servers[starts] = server;
}
void esp_sntp_init() {
  assert(!active && starts < 3 && servers[starts]);
  active = true;
  attempt_start = ticks;
  ++starts;
}
sntp_sync_status_t sntp_get_sync_status() {
  assert(active);
  return static_cast<uint32_t>(ticks - attempt_start) >= complete_after[starts - 1]
             ? SNTP_SYNC_STATUS_COMPLETED : SNTP_SYNC_STATUS_RESET;
}

int main() {
  // Exercise the complete unmodified production source at the host's native
  // unsigned-long wrap. On ESP32 that boundary is UINT32_MAX; LP64 hosts use
  // ULONG_MAX. This also makes the original absolute-deadline defect observable
  // without rewriting production source or relying on a 32-bit host toolchain.
  reset(ULONG_MAX - 2000UL, false);
  complete_after[0] = 0;
  if (!ClockSync::syncWithNtp(5000, true) || starts != 1 || writes != 1) {
    std::fprintf(stderr, "rollover: expected one successful NTP attempt, got %d starts and %d RTC writes\n", starts, writes);
    return 1;
  }
  check_stopped();

  // Run the target's actual 32-bit millis model on either host word size.
  for (uint32_t start : {100U, UINT32_MAX - 2000U, UINT32_MAX - 50U, UINT32_MAX}) {
    for (int winner = 0; winner < 3; ++winner) {
      reset(start);
      complete_after[winner] = 100;
      assert(ClockSync::syncWithNtp(5000, true));
      assert(starts == winner + 1 && writes == 1 && delays > 0 && spent <= 5000);
      check_stopped();
    }
    reset(start);
    assert(!ClockSync::syncWithNtp(5000, true));
    assert(starts == 3 && writes == 0 && spent >= 5000 && spent < 5100);
    assert(attempt_budget_used[0] == 1700 && attempt_budget_used[1] == 1700 && attempt_budget_used[2] == 1600);
    check_stopped();
  }

  for (uint32_t budget : {0U, 1U, 99U, 100U, 1499U, 1500U, 1501U}) {
    reset(UINT32_MAX - 50U);
    assert(!ClockSync::syncWithNtp(budget, true));
    assert(writes == 0 && spent >= budget && spent < budget + 100);
    assert(starts <= 3 && (budget != 0 || starts == 0));
    check_stopped();
  }

  reset(UINT32_MAX - 50U);
  valid = false;
  complete_after[0] = complete_after[1] = complete_after[2] = 0;
  assert(!ClockSync::syncWithNtp(5000, true));
  assert(starts == 3 && writes == 0);
  check_stopped();

  // A forced retry remains available after an RTC failure. The separate
  // ClockSyncRetryTest also covers initial failure followed by ordinary retry.
  reset(UINT32_MAX - 50U);
  complete_after[0] = 100;
  rtc_ok = false;
  assert(!ClockSync::syncWithNtp(5000, true));
  assert(starts == 1 && writes == 1);
  check_stopped();
  reset(UINT32_MAX - 50U);
  complete_after[0] = 100;
  assert(ClockSync::syncWithNtp(5000, true));
  assert(starts == 1 && writes == 1);
  check_stopped();
  reset(100);
  assert(ClockSync::syncWithNtp(5000, false));
  assert(starts == 0 && writes == 0);
  check_stopped();

  // A maximum representable timeout is legal; immediate completion is bounded.
  reset(UINT32_MAX - 50U);
  complete_after[0] = 0;
  assert(ClockSync::syncWithNtp(UINT32_MAX, true));
  assert(starts == 1 && writes == 1 && spent == 0);
  check_stopped();
  std::puts("Clock sync rollover, budget, failure, retry and cleanup tests passed");
}
