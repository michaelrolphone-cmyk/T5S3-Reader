# X4 Pro UC8279 ghosting and powered-idle laboratory firmware

This branch contains **X4LAB v0.1.7**, a standalone diagnostic derived directly from the hardware-validated **v0.1.5** implementation. It tests whether delayed old-image ghosting is caused by powered-idle panel bias, stale DTM1/DTM2 state, the 30-second resident maintenance refresh, or a charge-compensation sequence.

It does not boot RiscRTE, mount storage, start touch, or load installed drivers.

## Electrical boundary

Every mode remains inside the stable v0.1.5 envelope:

- recognized UC8279 ZHX `LUT_VER` `0x68` or `0x69` only;
- fixed 800×600 controller geometry with visible gates 120–599;
- complete visible 800×480 test region in every mode;
- ordinary full-width partial-window commands;
- 20 MHz hardware SPI only;
- PLL values `0x0E` and `0x0F` only;
- no TCON changes;
- no compact or remapped `TRES`/`GSST` geometry;
- no 40/80 MHz panel transport;
- no `0x3F` PLL probe;
- no voltage, VCOM, booster, PMIC, or battery changes;
- no deep-sleep command during the tests.

The destructive v0.1.6 scan/timing path is absent and unreachable.

The absolute one- and two-frame bank uses the corrected product-driver transition mapping: registers `0x21..0x24` are `0x81, 0x81, 0x41, 0x41` for a one-frame pulse.

## Common sequence

Every mode starts independently:

1. reset the controller;
2. write white across complete 800×600 DTM1 and DTM2 RAM;
3. perform one stock OTP white clean;
4. draw a full-visible asymmetric high-contrast pattern;
5. repeat resident pattern refreshes for 2.3 seconds;
6. hold that pattern without refresh for 30 seconds;
7. copy the pattern into DTM1 so it is the explicit OLD plane;
8. draw an absolute one-frame white clear into DTM2;
9. repeat resident white refreshes for 2.3 seconds;
10. apply the selected experiment and observe for 60 seconds.

The high-contrast image contains vertical bars, checkerboards, diagonal fields, asymmetric corner markers, horizontal fiducials, and a narrow white spine. Its inverse and all-white frame are precomputed once. Buffer hashes are printed at boot.

No screen update is made during the observation hold except the explicit mode-4 maintenance pulse and the named compensation operations. Progress is reported over serial every five seconds.

## Modes

| Mode | Experiment |
|---:|---|
| **1** | Leave the panel powered after white settling. No maintenance refresh for 60 seconds. Baseline for powered-idle accumulation. |
| **2** | Issue `POF` immediately after settling and remain off for 60 seconds. Direct powered-idle A/B comparison. |
| **3** | After settling, display the complete inverse of the burn pattern for one frame, then white for one frame, and remain powered. This is the requested raw inversion test. |
| **4** | Remain powered for 60 seconds and issue exactly one resident-image `DRF` at 30 seconds. Determines whether the product-style maintenance pulse corrects, relocates, or reinforces ghosting. |
| **5** | Rewrite complete DTM1 to white after settling, then remain powered. Isolates stale OLD-plane contribution without changing panel power. |
| **6** | Rewrite complete DTM1 to white, then issue `POF`. Candidate production idle transition. |
| **7** | Apply a changed-pixel reverse mask: redisplay the original pattern for one frame, restore white with a two-frame target-ending pulse, synchronize DTM1 to white, and remain powered. |
| **8** | Apply the same target-ending compensation and DTM1 synchronization, then issue `POF`. Candidate compensation-plus-power-off transition. |
| **9** | Synchronize DTM1, issue `POF` for 30 seconds, issue `PON` without any PTL/DTM/DRF command, then remain powered for 30 seconds. Measures POF/PON BUSY timing and tests whether analog power-on alone restarts drift. |

Mode 3 is intentionally different from modes 7–8. It inverts **every** pixel and performs only a one-frame white restoration. Modes 7–8 reverse only the pixels that changed in the pattern-to-white transition and finish with a stronger target-ending white pulse.

## Controls

- **Left or Right side button:** immediate IRAM hard abort. CS is raised, RESET is pulled low, clock/data outputs are stopped, and the firmware permanently halts until reboot.
- **Power button during a mode:** stop after the current physical refresh. The current panel state is left untouched for inspection.
- **Power button while ready:** run the next mode.
- Serial `n` or Space: run the next mode.
- Serial `1`–`9`: run a selected mode.
- Serial `s`: soft stop after the current refresh.
- Serial `q`, `x`, or Escape: hard abort.

Modes never auto-advance. At completion the current image and power state remain untouched. Starting the next mode performs a new reset and OTP white clean.

## Telemetry

Serial output records:

- every explicit framebuffer refresh and its upload/waveform duration;
- the tracked DTM1 and DTM2 contents;
- settling pulse count, elapsed time, average/minimum/maximum BUSY duration;
- five-second observation markers with power and BUSY state;
- the exact 30-second maintenance DRF;
- POF and PON BUSY/total durations;
- the PON-without-DRF boundary in mode 9;
- final mode state.

## Flashing

Use the merged image at offset `0x0`.

```sh
esptool.py --chip esp32s3 write_flash 0x0 x4-high-fps-lab-merged.bin
```

The merged image is a standalone laboratory installation. Preserve any device data that matters before flashing.
