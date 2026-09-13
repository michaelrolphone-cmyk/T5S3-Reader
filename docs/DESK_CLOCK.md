# Landscape sleep clock

Select **Settings → Display → Sleep Screen → Digital desk clock** (also exposed
by the shared web settings API as `sleepScreen: 6`). Existing Dark, Light,
Custom, Cover, None, and Cover+Custom values keep their original numeric IDs.
Power Off Screen retains its six static choices.

The sleep timeout now enters a large, landscape 24-hour clock with the local
date. This choice also permits the timeout while USB or a serial connection is
present, so the reader can be used as a powered desk clock. Other sleep choices
retain the existing USB/debug sleep suppression.

The clock uses the system time and configured timezone. If time is unset, it
shows dashes and asks you to set the clock instead of displaying a false time.
The explicit clock choice is independent of the Hide Clock option for normal
reader/home headers. The whole-display Flip UI setting still applies.

## Timing and power

`DeskClockSleep::run()` uses ESP32-S3 light sleep with a timer targeting the next
wall-clock minute boundary, plus PWR and supported touch GPIO wake. It keeps
RAM and initialized peripherals available rather than rebooting every minute.
The remaining interval is recomputed from `gettimeofday()` after rendering, so
display latency does not accumulate into a drifting 60-second cadence. A
refresh starts at the minute boundary; the e-paper panel takes its normal
finite time to finish the visible update.

Wi-Fi and the backlight are off during clock sleep. Panel drive power is gated
between refreshes without running the SD/touch deinitialization used for deep
sleep. RAM/SD/touch retention and minute refreshes consume more power than a
static image in deep sleep. Battery life and physical refresh latency must be
measured on the device.

Tap the screen where touch wake is supported, or press PWR, to leave the clock.
The wake gesture is consumed. Reader resumption follows the existing
Resume Reader on Boot preference and last-sleep reader state; otherwise Home
opens. Clock sleep does not maintain the previous Wi-Fi connection. Shutdown
still uses the existing Power Off Screen and does not run a clock.

## Implementation and checks

- `CrossPointSettings.h` and `SettingsList.h`: append the sleep-only enum value.
- `SleepActivity.cpp`: bypass static drawing for clock sleep.
- `DeskClockSleep.cpp`: procedural digits, exclusive framebuffer access,
  timer/user-wake loop, orientation restoration, and panel idle power handling.
- `main.cpp`: choose light-clock versus original deep sleep and restore the UI.
- Both display backends implement `setIdlePowerSaving()` without SD teardown.
- `test/run_desk_clock_test.sh`: verifies minute alignment, subsecond offsets,
  render-latency compensation, midnight, and a multi-day range; runs in CI.

Hardware acceptance: select each old sleep mode; check clock entry on battery
and USB; watch several :59→:00 transitions and midnight; test timezone and
unset-clock behavior; wake by touch/PWR; verify reader position and orientation;
test shutdown separately. Build/host tests cannot establish these hardware
behaviors.

The light-sleep design follows the [ESP-IDF 4.4 ESP32-S3 sleep API](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32s3/api-reference/system/sleep_modes.html).
