# X4 Pro panel driver

`x4pro-panel` provides the RiscRTE `display.output` capability for the Xteink X4 Pro. Version 0.2.0 preserves the existing SSD1677 backend and replaces the UC8279 path with the high-refresh architecture validated by X4LAB v0.1.5.

## 0.2.0 UC8279 operating envelope

The fast path is admitted only for the double-probed ZHX UC8279 `LUT_VER=0x68` variant. It uses:

- fixed 20 MHz ESP32-S3 native FSPI transport;
- the normal 800x600 controller geometry and established 120-row visible offset;
- ordinary UC8279 partial windows;
- PLL `0x0E` for clean refresh and `0x0F` for fast refresh;
- external one-frame and two-frame A2-style LUT profiles;
- separate logical framebuffer, DTM1-history, and panel-state tracking;
- bounded one-frame absolute bursts followed by a two-frame settle and DTM1 reseed;
- validated BUSY assertion/completion timing;
- automatic OTP clean fallback and fast-mode disable after repeated protocol faults.

The provider continues to expose ordinary MONO1 surfaces. Applications do not select LUTs, clocks, panel geometry, or cleanup sequences.

## Internal refresh profiles

- `OTP_CLEAN`: initial, explicit-clean, unknown-state, or fallback presentation.
- `DIFF_2F`: quality-oriented differential presentation.
- `DIFF_1F`: ordinary low-latency differential presentation.
- `ABS_1F`: bounded repeated-interaction burst with DTM1 synchronization deferred.
- `ABS_SETTLE_2F`: reinforces the final burst target and restores DTM1 synchronization.

Absolute bursts are limited to eight frames or 600 ms. A long gap, quality request, lifecycle transition, or limit crossing forces settlement. Repeated invalid BUSY cycles disable fast profiles for the remainder of the boot.

## Explicitly excluded destructive paths

The 0.2.0 production candidate contains no code path for the destructive X4LAB v0.1.6 experiments:

- no compact or remapped `TRES`/`GSST` geometry;
- no reduced or zero TCON timing;
- no 40/80 MHz production SPI;
- no undocumented PLL value such as `0x3F`;
- no application-provided LUT or panel-voltage control.

The v0.1.6 forensic history remains isolated at `quarantine/x4-high-fps-v0.1.6-destructive` and must not be flashed to usable hardware.

## Fault and lifecycle behavior

Every physical refresh requires BUSY_N to begin idle, assert within the profile deadline, remain asserted for a plausible physical interval, and complete before timeout. A missed, stuck, or implausibly short cycle fails the present and invalidates fast panel state.

Before sleep, an active absolute burst is settled and DTM1 is reseeded. The driver then powers the controller off, enters deep sleep, holds panel reset, and treats controller RAM as untrusted after restart.

## Status

Version 0.2.0 is an implementation candidate built at the user's direction without the multi-panel endurance qualification stage described in `docs/X4_UC8279_HIGH_REFRESH_GENERAL_DRIVER_PLAN.md`. Its manifest therefore remains `experimental-unpublished` until hardware integration testing is completed.
