#include <HalPowerManager.h>
#include <WiFi.h>
#include <cassert>
#include <cstdio>

TestWiFi WiFi;
static uint32_t frequency = 240;
static bool failClockChange = false;
unsigned long millis() { return 4000; }
uint32_t getCpuFrequencyMhz() { return frequency; }
bool setCpuFrequencyMhz(uint32_t requested) {
  // Model the actual S3 port's safe PLL configurations. Sub-80 MHz requests
  // change APB without the pinned Arduino layer noticing, even with no USB.
  assert(requested == 80 || requested == 160 || requested == 240);
  if (failClockChange) return false;
  frequency = requested;
  return true;
}
void HalGPIO::startDeepSleep(bool) {}

int main() {
  powerManager.begin();
  // Repeated idle/user activity cycles must preserve peripheral clocking.
  for (unsigned i = 0; i < 50; ++i) {
    powerManager.setPowerSaving(true);
    assert(frequency >= 80 && frequency < 240);
    powerManager.setPowerSaving(true);
    powerManager.setPowerSaving(false);
    assert(frequency == 240);
  }
  failClockChange = true;
  powerManager.setPowerSaving(true);
  assert(frequency == 240);
  failClockChange = false;
  powerManager.setPowerSaving(true);
  {
    HalPowerManager::Lock active;
    assert(frequency == 240);
    powerManager.setPowerSaving(true);
    assert(frequency == 240);
  }
  powerManager.setPowerSaving(true);
  WiFi.mode = 1;
  powerManager.setPowerSaving(true);
  assert(frequency == 240);
  puts("S3 idle/activity clock transitions preserve the PLL/APB domain: PASS");
}
