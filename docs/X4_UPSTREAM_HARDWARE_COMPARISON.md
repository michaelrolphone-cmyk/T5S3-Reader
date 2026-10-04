# X4 Pro upstream hardware comparison

Source comparison on 2026-10-04. This is not a device measurement or a claim
that changing peripheral initialization fixes the observed admission failure.

## Pinned primary implementations

- [CrossPoint develop a68548d9](https://github.com/crosspoint-reader/crosspoint-reader/tree/a68548d9c43793b90cff8e7bf8a8da735c1535e1)
  pins its FreeInk SDK submodule to aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a.
- [That FreeInk board profile](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/BoardConfig/include/BoardConfig.h#L1629)
  explicitly identifies X4 Pro as ESP32-S3. The plain X4 profile is ESP32-C3,
  uses different display/storage/button wiring, and has no RTC or gauge in
  that profile. It is not an interchangeable hardware reference.
- [TuyaOpen b1d3aecc](https://github.com/tuya/TuyaOpen/tree/b1d3aecc9a8399a436e123d06b081d705b109a65/boards/ESP32/XTEINK_X4_PRO)
  supplies another X4 Pro implementation. Its comments credit FreeInk/OEM
  findings, so agreement is code reuse, not independent physical validation.

## Hardware assignments and ownership

| Function | Pinned X4 Pro reference | Current Reader assignment |
| --- | --- | --- |
| Early peripheral rail | GPIO1 HIGH | Firmware boot-power owner, asserted before USB/SD |
| Shared I2C | SDA39 / SCL38 | One installed `i2c.bus` owner, same pins |
| Fuel gauge | CW2017, address 0x63 | `x4pro-battery`, same address |
| RTC | BM8563 / PCF8563, address 0x51 | `x4pro-rtc`, same address/register family |
| Touch | GT911 0x5D, optional 0x14; reset4, interrupt10 | Same primary address and pins |
| Touch rail | GPIO2 LOW, GPIO1 HIGH prerequisite | GT911 owns GPIO2; boot owns GPIO1 |
| SD rail | GPIO5 LOW while active | SD provider owns GPIO5 |
| SD bus | CLK41 / CMD42 / DAT0 40, one bit | Same pins and native SD protocol |
| Charging indicator | GPIO21 input, active HIGH, no pull | Same; not a VBUS/full-charge measurement |
| Frontlight | GPIO8 cool / GPIO9 warm, active HIGH | Same; new0.1.3 provides actual PWM |
| Power-button wake | GPIO3 active LOW, RTC ext1 | Same source and polarity |

The source profile is authoritative over a stale line in the hardware notes
that still reverses GT911 interrupt/reset labels. Current profile and the
notes' detailed touch section agree on interrupt10/reset4.

[CrossPoint setup](https://github.com/crosspoint-reader/crosspoint-reader/blob/a68548d9c43793b90cff8e7bf8a8da735c1535e1/src/main.cpp#L430)
asserts its rail before serial and peripheral initialization. Reader now does
the same through its one boot-power owner, while retaining output configuration
before releasing an existing pad hold. The upstream sequence is evidence for
pin/rail requirements, not a reason to undo Reader's reviewed hold ordering.

## Where the current failure occurs

Reader's optional battery and RTC requests pass through a structurally inspected
installed capability snapshot, dependency resolution, package registration,
ELF activation, bus claim and only then chip reads. The battery dependency
chain is `board.battery -> i2c.bus -> platform.clock`; RTC shares its last two
steps. Required boot providers use the selected board boot profile.

The supplied 1.3.102 log reports a completed inventory and zero-ms registration
refusal before any battery activation. A clean copy of the exact delivered nine
packages resolves correctly in the real SD/FatFs/HAL fixture. Rejected metadata
or a stale storage generation reproduces that early failure; hardware pin or
register changes cannot repair a refusal before chip access. Diagnostics now
identify the rejected stage/entry or unresolved dependency without extra reads.

## Battery initialization difference

[FreeInk BatteryMonitor](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp#L200)
checks the CW2017 mode/version, profile flag at0x0B and resident80-byte BATINFO
at0x10..0x5F. A verified mismatch writes its recovered OEM profile; I2C failure
does not itself authorize a rewrite. It then performs the documented mode
sequence0xF0 -> 0x30 -> 0x00 when needed, waits boundedly for running version
0x0D/0x0F and a valid percentage, and retries failed initialization later.
[Tuya's corresponding driver](https://github.com/tuya/TuyaOpen/blob/b1d3aecc9a8399a436e123d06b081d705b109a65/boards/ESP32/XTEINK_X4_PRO/xteink_x4_pro_battery.c)
uses the same recovered profile and reset concept.

Reader's battery0.1.2 is read-only. It checks running version, normal mode at
0x08, voltage and percentage, but cannot initialize an absent/mismatched profile
or wake a stopped calculation engine. This is a real downstream coverage gap,
not proof of the present card's state. A future repair needs a board-qualified
profile, bounded staged initialization, exact I2C error/ownership handling and
tests for interrupted writes and readiness. The current increment does not
write or replace battery calibration data.

## RTC and shared-bus differences

[FreeInk RTC](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/Rtc/src/Rtc.cpp)
probes control0 at0x51 and disables CLKOUT at0x0D. It rejects the voltage-low
flag before returning time. Explicit synchronization writes the calendar and
clears that flag. Disabling CLKOUT does not establish valid time.

Reader uses the same register family through its existing versioned RTC
capability, additionally validates STOP/BCD/calendar values, and leaves startup
registers untouched. It reads optional RTC before starting the independent
touch task and uses retained SDK time for minute wakes. Both implementations
need valid RTC data or explicit synchronization; no build-time clock should be
invented to hide invalid state. Reader's current log fragment omits RTC startup,
so admission versus unset-time remains unresolved for that request.

FreeInk uses hardware Wire at400kHz on the shared bus. Reader's installed I2C
provider owns bounded software transactions on the same pins and serializes
claims/operations across RTC, gauge and touch. Its stricter zero-wait contention
failure can be reported separately after activation; it is not evidence of an
incorrect pin map. No peripheral firmware proxy or parallel Wire owner is added.

## Underlying SD throughput limitation

The [FreeInk profile](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/BoardConfig/include/BoardConfig.h#L1714)
documents native hardware SDMMC and40MHz, but its actual
[SdmmcBlockDevice](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp#L48)
sets `SDMMC_FREQ_DEFAULT`. CrossPoint's pinned pioarduino55.03.311 selects
IDF5.5.5, whose [header](https://github.com/espressif/esp-idf/blob/v5.5.5/components/sdmmc/include/sd_protocol_types.h#L217)
defines that default as20000kHz (20MHz), distinct from the40000kHz high-speed
option. The framework macro, not the40MHz comment, determines the requested
host ceiling; actual negotiated/electrical rate remains unmeasured here.
Reader SD0.2.3 uses the same native one-bit
protocol/pins but clocks each bit in software. Its
[selected-phase guards](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/60e5a78b79e5a196e2265aa8bad3200af5a82479/Drivers/x4pro_sd/driver.c#L46)
require GPIO readback and24CPU cycles per HIGH/LOW phase. At the maximum240MHz
CPU clock, the guards alone cap clock frequency at5MHz before software/MMIO
overhead. This is a source-derived bound, not a measured transfer rate.

This transport cost is separate from the measured redundant directory reads.
The performance-ledger coordinator received these pinned references for
deduplication. No SD rewrite is included in this increment; any later native
controller implementation belongs inside the existing installed SD provider
and must preserve its lifetime, CRC, bounded-I/O and recovery contracts.
