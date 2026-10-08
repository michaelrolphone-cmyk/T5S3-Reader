# X4 Pro panel provider

`x4pro-panel` supplies the `display.output` v1 capability for Xteink X4 Pro
boards containing either the SSD1677 or ZHX UC8279 panel controller.

## Version 0.2.0

The SSD1677 implementation is unchanged. The UC8279 `LUT_VER=0x68` path is
replaced by the general-use implementation derived from X4LAB v0.1.5.

The fast engine keeps the validated electrical boundary:

- native ESP32-S3 SPI2 transport at 20 MHz;
- normal 800×600 UC8279 geometry with the visible image at gate offset 120;
- ordinary partial-window commands;
- PLL `0x0e` for clean operation and `0x0f` for fast operation;
- external one-frame and two-frame A2-style LUTs;
- differential one/two-frame profiles;
- bounded one-frame absolute bursts that omit DTM1 synchronization;
- two-frame settle plus DTM1 reseed when leaving an absolute burst;
- verified BUSY assertion, plausible active duration, and bounded completion;
- OTP clean/reseed fallback after unknown state, clean intent, or refresh budget;
- session-level fast-mode disable after repeated protocol faults;
- normal `display.output` MONO1 frame and damage-rectangle handling.

The provider deliberately contains no production path for the destructive
v0.1.6 experiments: no compact/remapped TRES/GSST geometry, reduced TCON,
40/80 MHz panel SPI, undocumented `0x3f` PLL, voltage changes, or
application-supplied LUT data.

The driver preserves application MONO1 pixels exactly. It does not synthesize
temporal Bayer gray phases; renderers may still submit pre-dithered MONO1
content.

## Build

```sh
RISCRTE_X4_LINK_PROFILE=esp14-no-relax \
python scripts/build_x4pro_drivers.py --source x4pro_panel
```

Output:

```text
dist/experimental/x4pro-panel/driver.elf
dist/experimental/x4pro-panel/manifest.json
```

## Qualification status

Version 0.2.0 is an implementation increment built from the stable v0.1.5
mechanism. The requested final multi-panel endurance/optical qualification
phase was skipped. Keep the package `experimental-unpublished` until hardware
integration and lifecycle testing are complete.
