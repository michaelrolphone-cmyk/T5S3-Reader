# X4 Pro fine frontlight control

## Defect and compatibility

At Reader `60e5a78b`, `BoardX4Pro::setBacklightLevel` converted every nonzero
saved 0–10 setting to `(1, 1)`. `x4pro-frontlight@0.1.2` then drove both GPIO8/9
HIGH. Levels 1, 2 and 3 therefore requested identical static output, not PWM.
This is source evidence, not a measurement of emitted brightness or current.

The fix retains the saved setting, Settings range `{0, 10, 1}`, Global Menu,
web setting, `display.frontlight@1` ABI and `x4pro-frontlight` package identity.
The board adapter sends a fine ratio through the existing `set_level` API;
the installed **0.1.3** ELF alone programs the hardware. Install the matching
firmware adapter and driver together: updating only firmware leaves 0.1.2's
binary behavior; updating only the driver leaves the old adapter's `(1, 1)`.
No GameBoy or T5 driver files are changed.

## GameBoy reuse and mapping

[GameBoy `night_light.cpp` at 46e74ba4](https://github.com/michaelrolphone-cmyk/T5S3-GameBoy/blob/46e74ba453243921695b29e06f5eaa25453645de/src/night_light.cpp)
represents duty in tenths of a percent and uses 10-bit PWM; its low range steps
by 0.1% below 1%. Its older `docs/NIGHT_LIGHT.md` is stale. Reuse that fine-duty
representation, not GameBoy's GPIO11, PT4103B23F assumptions, 1 kHz frequency,
LEDC timer2/channel2 or private persistence. Reader already owns persistence
and exposes a ratio-capable frontlight API.

Reader retains eleven positions and allocates four nonzero positions to the
night range. The lookup is a perceptual-style control curve, not a calibrated
luminance curve. Each listed output is applied equally to cool and warm to
preserve the previous dual-HIGH maximum; color-temperature mixing is unchanged.

| Saved level | Requested duty per channel | 10-bit duty count (period 1024) |
| --- | --- | --- |
| 0 | 0% | 0, GPIO LOW held |
| 1 | 0.1% | 1 |
| 2 | 0.2% | 2 |
| 3 | 0.5% | 5 |
| 4 | 1% | 10 |
| 5 | 2% | 20 |
| 6 | 5% | 51 |
| 7 | 10% | 102 |
| 8 | 25% | 256 |
| 9 | 50% | 512 |
| 10 | 100% | 1024, continuous HIGH |

Integer conversion rounds to nearest count; positive sub-count ratios clamp
to 1 and nonmaximum ratios to at most 1023. Invalid zero denominators and
numerators above the denominator fail without changing the previous output.
The provider getter returns the last programmed duty as `count/1024`, not an
inconsistent logical value with maximum 1. Values above saved level 10 clamp
to 10. Actual lowest duty is 1/1024 (about 0.0977%); neither emitted luminance,
LED current, minimum reliably visible level nor flicker has been measured.

## Verified X4/CPU contracts and resource scope

[FreeInk X4 hardware notes at 111fdcc7](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/docs/xteink-x4pro-support.md#frontlight--dual-warmcold-pwm)
and its [board profile](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/libs/hardware/BoardConfig/include/BoardConfig.h)
identify active-high cool GPIO8 and warm GPIO9, with 25 kHz/10-bit recovered
from OEM 7.0.8. The earlier bring-up image used 10 kHz. Use the pinned profile's
25 kHz, not T5's frequency. [The corresponding FrontlightManager](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/libs/hardware/FrontlightManager/src/FrontlightManager.cpp)
reserves LEDC channels 0/1 for frontlight in the Arduino 2.x path.

This X4 ELF reserves low-speed timer0 and channels0/1, GPIO8/9. No current
other X4 provider uses these resources. Start rejects active claimed channels,
other active channels sharing timer0, an unset global source with any active
channel, and the calibrated RTC source. Another active timer with a known
APB/XTAL source is preserved. These checks supplement the static board resource
reservation; they are not a general arbitration scheme for arbitrary software
that ignores that reservation.

Register fields are from ESP-IDF **v4.4.7**, ESP32-S3:

- [ledc_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32s3/include/soc/ledc_reg.h): channel stride 0x14; timer0 +0xA0; global source +0xD0; timer resolution/divider/reset/update and channel enable/update/fade fields.
- [ledc_ll.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32s3/include/hal/ledc_ll.h): 8 fractional divider bits, clock source 1=APB and 3=XTAL, integer duty shifted left 4; full duty `2^resolution` is valid at 10 bits (below the S3 maximum resolution).
- [gpio_sig_map.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32s3/include/soc/gpio_sig_map.h): LEDC channels0/1 output indices73/74.
- [system_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32s3/include/soc/system_reg.h): LEDC clock/reset bit11. No peripheral-wide reset pulse is used.

Divider = sourceHz * 256 / (25000 * 1024): 400 for 40 MHz XTAL or 800 for
80 MHz APB. Unused LEDC selects XTAL. Reader's existing S3 power profile keeps
APB at 80 MHz in active/idle CPU modes. No source switching/retiming occurs
while another channel is active. Only owned channels/timer and two GPIO pads
are changed; unrelated registers/bits are preserved.

## Synchronization and retained failures

The ELF reuses `Drivers/x4pro_i2c/os_cpu_v1.h` and the same supported
nonrecursive, zero-wait RTOS mutex pattern used by X4 SD/I2C. Provider BSS can
be placed in PSRAM: bare atomic test-and-set emits raw Xtensa S32C1I and is
unsafe there. Only atomic load/store fences are used for lifecycle publication;
all read-modify-write synchronization belongs to the RTOS.

The exact six preexisting privileged OS/CPU ABI1 imports are
`xQueueCreateMutex`, `xQueueSemaphoreTake`, `xQueueGenericSend`, `vQueueDelete`,
`xPortInIsrContext`, and `xTaskGetCurrentTaskHandle`. No firmware driver proxy,
loader privileges or ABI symbol is added. The builder compiles their shared
declarations against the pinned SDK when available; CI requires that SDK.
The production exact-import matcher also rejects raw S32C1I in this ELF.

ISR, no-task, busy and recursive operations fail with no MMIO. Allocation
precedes any hardware access. Owner mismatch or failed mutex give poisons and
retains the mutex/provider generation until reboot; subsequent operations and
quiescence fail instead of reporting safe unload. Quiescence closes admission
before giving the mutex, and publishes acceptance only after a successful give.
Only final serialized stop, after consumer revocation and accepted quiescence,
deletes the mutex. A failed give can follow an already-applied brightness
request; the false return does not promise that request was rolled back.

## Lifecycle and verification

Start is dark and releases stale retained pad holds only after programming
LOW. Off disconnects both PWM routes and holds both GPIOs LOW across sleep.
The existing ordinary sleep, desk-clock, failed-sleep recovery, boot and
native-app return paths all call the same Board adapter. Nonzero restore
releases the holds and reconnects PWM at the same saved level. Quiesce makes
the owned pads dark, disables owned channels when configuration is intact,
and never resets the global timer/peripheral. A changed timer/channel fails
dark rather than stealing ownership back. Driver replacement requires normal
quiescence; a stopped provider rejects level calls.

Run `test/run_x4pro_frontlight_test.sh`: production C provider plus production
BoardX4Pro adapter, fake MMIO, ASan/UBSan. It covers every saved level, 1001
fine requests, all 65536 numerators with denominator65535, rounding/bounds,
invalid requests, missing/incompatible provider, off/restore/restart,
configuration loss, clock selection, no-write conflict rejection and unrelated
timer preservation, ISR/no-task/allocation/take failure, recursive admission,
independent-task contention, a delayed final-give race, and start/read/write/
quiesce give failures plus owner mismatch. Poison cases use separate processes
so tests never reset a retained generation in place. In environments where LeakSanitizer cannot run under
ptrace, run with `ASAN_OPTIONS=detect_leaks=0`; address/undefined checks remain.
The shared installed-provider package tests check ZIP/catalog/SHA/imports.
Software checks do not establish physical PWM, luminance or sleep current.
