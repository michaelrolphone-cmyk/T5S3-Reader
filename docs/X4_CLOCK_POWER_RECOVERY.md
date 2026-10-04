# X4 clock, battery-only boot and wake recovery

## Report and scope

The owner reports a visible `--:--` sleep clock, no useful button wake, and
clarifies that the X4 Pro does not boot without USB. This is a failed physical
test of the earlier candidate. The latest log identifies the matching SD
provider 0.2.2 but contains normal USB startup only, no sleep/wake record.
These source repairs do not establish the physical cause or a passing device.

## Board-alive sequencing

The previous source first drove GPIO1 HIGH in the GT911 provider, about 4.46 s
into the supplied normal boot log. The minute-wake path intentionally never
starts touch, so it had no GPIO1 writer. No GPIO1 hold survived deep sleep.

The isolated `X4BootPower` board-alive bootstrap now runs before the USB wait,
SD bootstrap, or clock-wake dispatch. Its five checked GPIO operations preload
HIGH, configure this one pad as a digital output, assert HIGH, release an old
hold only after that output configuration is restored, and re-hold it. This
ordering follows the IDF `gpio_hold_dis` guidance to avoid a low/floating gap
after deep reset. `gpio_config` selects the digital mux through `rtc_gpio_deinit`;
that operation does not release the pad hold. ESP-IDF 4.4.7 routes `gpio_hold_en` for RTC-capable
GPIO1 to `rtc_gpio_hold_en`; this is independent of the digital-only
`gpio_deep_sleep_hold_en` facility. It has no off API, capability, polling,
peripheral protocol or shared-controller ownership. Failure prevents package
startup. The GT911 ELF no longer writes GPIO1 and remains sole owner of the
independent active-low GPIO2 touch switch. SD still owns its GPIO5 switch.

Primary reference evidence:

- [Pinned FreeInk board profile and early latch helper](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/libs/hardware/BoardConfig/include/BoardConfig.h)
  asserts GPIO1 before peripheral setup. Its old comments overstate SD/panel
  dependence relative to the corrected bench notes, so those comments alone
  are not treated as physical proof.
- [Pinned ESPHome Xteink implementation](https://github.com/vjFaLk/esphome-xteink/blob/ad3aadca23403285504c4362889bbc5eee9d13e9/components/xteink/xteink.cpp)
  identifies X4 Pro GPIO1 as a keep-alive, asserts it first and holds it HIGH
  through sleep, while switching touch/SD off independently.
- [Pinned IDF GPIO implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gpio.c)
  provides the actual checked mux/hold behavior used by this target.

The necessity of retaining a boot-alive pad before loading its SD-resident
providers is an isolated bootstrap exception, not a new firmware power driver.
No board shutdown policy or new hardware-register writer is added to the core.

## Clock and wake behavior

The [pinned hardware notes](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/docs/xteink-x4pro-support.md#input--digital-buttons--capacitive-home)
confirm active-low power GPIO3 and side buttons GPIO0/7. GPIO0 is a reset strap.
Capacitive Home belongs to the powered-off GT911. The retained clock intentionally
arms only power GPIO3 and its minute timer. GPIO3 is RTC-capable on S3; the checked
EXT1 ANY_LOW and RTC pull-up/domain setup already match
[IDF 4.4.7](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/sleep_modes.html).
There is no evidence for changing that pin or enabling all buttons.

`--:--` is the existing face's invalid-system-time state. Earlier X4 cold startup
configured timezone but did not read its BM8563 RTC. A timer wake with invalid
SDK time now returns to ordinary startup before display package loading; it
does not invent an epoch. The six faces and minute timer engine remain shared.

The repaint-button software-restart marker was erroneously `RTC_DATA_ATTR`.
The [IDF image loader](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bootloader_support/src/esp_image_format.c)
reloads that initialized segment on software reset. It is now `RTC_NOINIT_ATTR`,
accepted only on `ESP_RST_SW`, consumed once and scrubbed on other boot paths.
This preserves the existing skip-splash intent; it does not prove that it caused
the reported battery-only failure.

Boot logs now include firmware version/board-alive result. Clock logs record
wake cause, retained-state/time validity, entry and armed minute/button sources.

## Verification boundary

The production bootstrap runs against a held-pad host model covering cold and
stale-held pins, deep reset, repetition, each failed GPIO operation and retry.
Its modeled external pull-down forces an unheld/non-output pad LOW and checks
every edge for HIGH-to-LOW glitches. The initial repair released hold before
restoring output enable; independent review caught that error. The earlier
ordering fails this strengthened test and the corrected ordering passes.
The production clock dispatcher covers cold/button/timer wake, valid/unset time,
pad/provider/frame/repaint failure, and one-shot software-reset intent. Reverting
only the marker attribute makes the software-reset assertion fail. The actual
GT911 fixture rejects every attempt to write GPIO1 and retains existing
touch/Home/gap and failed-release tests. Existing six-face/minute tests pass.

Host pad/reset models do not emulate battery power, ROM/bootloader analog
timing, the actual card, RTC drift or panel current. Battery-only cold start,
minute repaint and power-button return remain physical tests, not inferred
passes. Existing delivered images remain immutable.

## RTC and provider-discovery integration

The matching SD set now includes optional `x4pro-rtc 0.1.0`, reusing Watch's
existing `rtc.clock` API2 layout and PCF8563 calendar implementation for the
verified BM8563 at 0x51. `HalClock` retains its timezone/reference conversion,
while all X4 register I/O lives in the ELF behind the existing I2C bus owner.
Normal startup can recover valid external time; absent/stopped/VL/invalid time
stays unset. Startup performs no RTC write. Existing explicit synchronization
writes UTC and checks read-back. Minute refresh still uses retained SDK time.
The one-shot cold-boot RTC read precedes GT911's independent capture task so
ordinary touch polling cannot make the zero-wait I2C bus reject that read.
Failed initial admission is not immediately repeated by the cold-boot path.
RTC acquisition is owner-task/reentry guarded; ordinary calls release their
lease, and uncertain release blocks sleep until checked cleanup succeeds.
The shared sleep transition suspends RTC before navigation's graph drain and
rolls back that suspension on refusal. See the [RTC contract](../Drivers/x4pro_rtc/README.md).

The battery package was already installed but intentionally absent from the
mandatory seven-driver bootstrap list. Its ordinary capability lookup could
fail before the gauge code ran: the shared installed-provider scan discarded
its entire snapshot after only two seconds per directory root. Actual delivered
metadata, X4 provider/FatFs/HAL and injected sector latency reproduce this
failure at 2.20 seconds. The supplied boot log's gap is consistent with that
cause, without proving physical sector timing.

The per-root scan budget is now a finite 15 seconds, with an explicit timeout
log; entry, file-size, provider I/O, scheduler checkpoints and cache-generation
rules remain intact. It never publishes/caches a partial timeout result.
The durable nine-provider timing test covers healthy slow acquisition, zero-I/O
warm reuse, genuine over-budget refusal/retry, optional RTC absence/malformed
metadata and directory failure. This corrects software admission; a sleeping,
absent or invalid CW2017 can still report unavailable honestly.

## SD clock register traffic

`x4pro-sd 0.2.2 -> 0.2.3` configures the selected card's CLK direction/mux once
and changes only its level during transfers. This removes repeated register
configuration at each edge. The initial ungated optimization was withheld:
back-to-back APB writes alone did not prove a safe pulse width.

The final path observes each HIGH and LOW through GPIO input read-back, then
waits at least 24 unsigned CCOUNT cycles. The S3's 240 MHz CPU ceiling gives
a minimum 100 ns after the observed edge, below 5 MHz before software overhead,
within default SD's 25 MHz ceiling and 10 ns HIGH/LOW minima. Both polls have
128-iteration caps; stuck read-back/counter failures propagate as media I/O
failure without retrying uncertain writes. Identification-mode sequencing is
unchanged. Sources: [Espressif TRM, sections 6.4.4 and 7.2.4.1](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf),
[manufacturer SD timing, section 4.2.1](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6165/FDMS008GCA0.pdf).

The real GPIO helper/register-boundary model checks read/write/CRC and phase
spacing, wrap-around, stuck counter/read-back, bootstrap power holds and existing
storage failure/lifetime behavior. Target disassembly confirms GPIO read-back,
CCOUNT comparisons and both bounded loops. Reduced register writes are measured
in the model; electrical waveforms and device launch speed remain unmeasured.
