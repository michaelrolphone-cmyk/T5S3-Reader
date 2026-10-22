# X4 Pro UC8279 Ghost / Powered-Idle Laboratory Firmware

Standalone firmware for determining whether delayed ghost accumulation on an **Xteink X4 Pro with UC8279 ZHX controller** is caused primarily by powered-idle analog bias, stale DTM1/DTM2 state, a recurring resident maintenance refresh, or insufficient target-ending charge compensation.

This is **v0.1.7**, branched directly from the hardware-tested v0.1.5 source commit `05d811ae3a75b0711540484ccdbee32464042dd6`. It deliberately excludes the quarantined v0.1.6 compact-geometry/TCON/PLL experiments.

## Electrical boundary

Every test uses the same conservative controller envelope:

- 20 MHz SPI only;
- normal 800×600 controller geometry;
- visible 800×480 image at gate offset 120;
- ordinary full-width partial windows;
- PLL `0x0F` for one-frame/two-frame external A2 operations;
- PLL `0x0E` for the OTP baseline;
- no TCON programming;
- no compact TRES/GSST remapping;
- no 40/80 MHz transport;
- no undocumented PLL values;
- no panel-voltage, VCOM-level, booster, PMIC, or battery changes.

The absolute one-frame LUT uses the product-driver polarity correction: LUT registers `0x21..0x24` are `0x81,0x81,0x41,0x41` for a one-frame pulse. Both new-white transition buckets drive white and both new-black buckets drive black.

## Immediate abort

Either side button is an electrical hard abort. Its ISR immediately:

1. raises EPD CS;
2. asserts panel RESET low;
3. forces SCLK, MOSI, and DC low; and
4. leaves RESET held low until reboot.

USB serial `q`, `x`, or Escape invokes the same halt. The power button is not the emergency abort; it starts a mode and can shorten the 60-second observation after all physical setup operations finish.

## Identical common sequence

All nine modes perform the same full-panel preparation before their one differing action:

1. Reset and initialize the controller using normal 800×600 geometry.
2. Perform a full OTP refresh to the fixed high-contrast 800×480 diagnostic image.
3. Synchronize DTM1 to that pattern.
4. Reassert the pattern using the corrected one-frame absolute waveform.
5. Repeatedly drive the resident pattern for 2.3 seconds without retransmitting pixels.
6. Upload and display a full white target with the same one-frame absolute waveform.
7. Repeatedly drive the resident white target for 2.3 seconds.

At that point the visible target and DTM2 are white, while DTM1 still contains the high-contrast pattern unless the selected mode explicitly synchronizes it. The mode then performs its unique action and holds the cleared display for 60 seconds. Nothing is drawn during the observation except the one explicitly scheduled maintenance pulse in mode 6 or the PON-only event in mode 9.

The white inspection image remains on the glass when a mode completes. Inspect or photograph it before starting another mode; the next mode necessarily overwrites it during its baseline.

## Test matrix

| Mode | Action after the white 2.3-second settle | Question isolated |
|---:|---|---|
| 1 | Leave panel powered; stale pattern remains in DTM1; no maintenance | Powered-idle baseline |
| 2 | Issue POF; leave stale DTM1 untouched | Does removing analog panel power stop accumulation by itself? |
| 3 | Display the inverse of the original pattern for one frame, then white for one frame; remain powered | User-requested raw inverse-pulse experiment |
| 4 | Upload full-controller white to DTM1; remain powered | Is stale OLD-plane state necessary for accumulation? |
| 5 | Synchronize DTM1 to white, then issue POF | Recommended final-state sequence |
| 6 | Remain powered and issue exactly one full-visible resident DRF at 30 seconds | Does the product driver’s awake maintenance pulse improve, move, or worsen the ghost? |
| 7 | One inverse frame followed by a **two-frame final-white** pulse; remain powered | Target-ending compensation versus the raw mode-3 inversion |
| 8 | Assert controller RESET low for the whole observation | Controlled version of the manual reset/off-state observation |
| 9 | Issue POF for 30 seconds, then PON only—no upload and no DRF—for the final 30 seconds | Does analog power-on alone restart the drift, and how long do POF/PON take? |

Mode 3 intentionally performs the exact raw inverse/restore experiment. Mode 7 is the safer comparison: its last physical operation is a longer final-white drive, so it does not leave the particles at the end of the reverse pulse. Neither mode uses the previously failed `4+1` or `4+2` counterpulse LUTs.

## Telemetry

Serial output at 115200 records:

- controller FLG/VER identity;
- every PON and POF BUSY interval;
- every target upload and DRF BUSY interval;
- the number and average duration of 2.3-second resident settling pulses;
- DTM1 synchronization time;
- panel power state and BUSY_N every five seconds during observation;
- the exact 30-second maintenance or PON-only event; and
- the final retained inspection state.

A PON, POF, or DRF that fails to assert BUSY_N within 100 ms or complete within its bounded timeout is a failed mode and triggers the hard-reset halt. Implausibly short BUSY cycles are not accepted as completed operations.

## Controls

- **POWER:** run the next mode.
- **POWER during observation:** end the 60-second observation early, retaining the current image/state.
- **LEFT or RIGHT side button:** immediate hard abort and permanent RESET-low halt.
- **Serial `n` or space:** run the next mode.
- **Serial `1`–`9`:** run a selected mode.
- **Serial `q`, `x`, or Escape:** hard abort.

## Build

```bash
pio run -c platformio.x4lab.ini -e x4-high-fps-lab
```

The post-build step produces:

- `.pio/build/x4-high-fps-lab/firmware.bin` — application image;
- `.pio/build/x4-high-fps-lab/firmware-merged.bin` — complete offset-0 flash image;
- `.pio/build/x4-high-fps-lab/firmware.elf` — symbols/debug image.

Flash `firmware-merged.bin` at offset `0x0`.
