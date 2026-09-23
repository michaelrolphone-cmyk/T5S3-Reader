# Landscape sleep clock

Select **Settings → Display → Sleep Screen → Digital desk clock** (also exposed
by the shared web settings API as `sleepScreen: 6`). Existing Dark, Light,
Custom, Cover, None, and Cover+Custom values keep their original numeric IDs.
Power Off Screen retains its six static choices.

The sleep timeout now enters a large, landscape digital clock with the local
date. **Settings → System → Time Format** selects **12-hour (AM/PM)** (the
default, including upgrades) or **24-hour (military)**. This saved option applies
to the desk clock, Home header and reader status bar in every theme. Midnight
is `12:00 AM` and noon is `12:00 PM`; 24-hour mode shows `00:00` and `12:00`.
The shared web settings API exposes `timeFormat: 0` for 12-hour and `1` for
24-hour. Existing clock visibility settings still control the normal UI.

This sleep choice also permits the timeout while USB or a serial connection is
present, so the reader can be used as a powered desk clock. Other sleep choices
retain the existing USB/debug sleep suppression.

The clock uses the system time and configured timezone. If time is unset, it
shows dashes and asks you to set the clock instead of displaying a false time.
The explicit clock choice is independent of the Hide Clock option for normal
reader/home headers. The whole-display Flip UI setting still applies.

## Timing and power

`DeskClockSleep::run()` paints the first frame, then enters ESP32-S3 deep sleep
with a timer targeting the next wall-clock minute boundary plus PWR-button wake.
The e-paper image persists without power while ordinary RAM, initialized
peripherals, SD, touch, Wi-Fi, GPS/LoRa and the backlight are shut down.

The retained RTC state records the minute that is physically displayed. On a
timer boot, the firmware performs only the minimum board/RTC/display setup. For
ordinary minute changes it renders that retained minute into the logical
framebuffer, copies the 1-bit result to a temporary PSRAM buffer, renders the
new minute, and compares the two frames. Because M5GFX's EPD history is also
lost in deep sleep, RiscRTE first primes that driver's history for the dirty
rectangle with the reconstructed previous pixels while the real panel output
rails are suppressed. It then submits the new dirty rectangle normally. This
normally limits physical panel drive to the changing minute digit instead of
redrawing or clearing the whole 960×540 screen. Hour, AM/PM, and date
transitions naturally expand the dirty rectangle. A periodic full refresh is
still performed every 30 clock updates to control e-paper ghosting.

The remaining sleep interval is recomputed from `gettimeofday()` after each
visible update, so display latency does not accumulate into a drifting
60-second cadence. If a refresh crosses a minute boundary, the clock immediately
renders the newly current minute before sleeping again.

Press PWR to leave the clock. The wake press is consumed by the normal boot
path. Reader resumption follows the existing Resume Reader on Boot preference
and last-sleep reader state; otherwise Home opens. Shutdown still uses the
existing Power Off Screen and does not run a clock.

## Implementation and checks

- `CrossPointSettings.h` and `SettingsList.h`: append the sleep-only enum value.
- `SleepActivity.cpp`: bypass static drawing for clock sleep.
- `DeskClockSleep.cpp`: procedural digits, retained displayed-minute state,
  reconstructed previous-frame buffer, deep-sleep timer/PWR wake, and
  orientation restoration.
- `HalDisplay.cpp`: differential framebuffer comparison and clipped T5S3
  panel submission for partial clock refreshes.
- `main.cpp`: choose light-clock versus original deep sleep and restore the UI.
- Both display backends implement `setIdlePowerSaving()` without SD teardown.
- `test/run_desk_clock_test.sh`: verifies minute alignment, subsecond offsets,
  render-latency compensation, midnight, and a multi-day range; also checks
  12/24-hour formatting, noon/midnight and buffer bounds; runs in CI.

Hardware acceptance: select each old sleep mode; check clock entry on battery
and USB; watch several ordinary minute transitions and verify only the changed
clock region refreshes; watch :59→:00, noon/midnight, and the periodic full
refresh; test timezone and unset-clock behavior; wake by PWR; verify reader
position and orientation; test shutdown separately. Switch Time Format both
ways, restart to check persistence, and check AM/PM spacing in all themes and
both status bar positions. Build/host tests cannot establish panel waveforms or
battery current.

The light-sleep design follows the [ESP-IDF 4.4 ESP32-S3 sleep API](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32s3/api-reference/system/sleep_modes.html).
