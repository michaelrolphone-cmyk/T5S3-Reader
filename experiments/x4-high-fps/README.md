# X4 Pro high-FPS panel laboratory

Standalone firmware for experimentally driving the **UC8279 ZHX** Xteink X4 Pro panel outside the Reader runtime. It does not mount the SD card, load RiscRTE drivers, start touch, or enter the normal application firmware.

## Physical abort

**Press either side navigation button at any time.** Both buttons are connected to an IRAM GPIO interrupt. The interrupt immediately:

1. pulls panel `RST` (GPIO14) LOW;
2. forces panel `CS` (GPIO13) HIGH;
3. stops the panel clock/data/DC outputs;
4. latches an abort flag; and
5. leaves the firmware permanently halted with reset held LOW until the device is rebooted.

The SPI stream is divided into eight-row chunks, limiting the software-side interval before the foreground task also observes the abort to roughly 0.32 ms at 20 MHz. The interrupt itself asserts panel reset without waiting for the current SPI transaction or BUSY cycle.

The **power button** starts the next test mode. During a run it requests a soft stop after the current refresh. USB serial `q`/`x` is another hard-abort path.

## Controller gate

The firmware probes `FLG` (`0x71`) and `VER` (`0x70`) twice before enabling hardware SPI. It refuses to run unless both reads agree, BUSY_N reports idle, and `VER[2]` is **0x68 or 0x69**. Those are the ZHX UC8279 LUT IDs this experiment targets. SSD1677, UC8179, QY UC8279, ambiguous, and floating-bus results halt with panel reset held LOW.

## Operation

1. Flash the merged artifact at offset `0x0`.
2. Open USB serial at 115200 baud.
3. Reboot without holding the left button; GPIO0 is also a boot strap.
4. The firmware performs one OTP full-clean baseline and waits.
5. Press the power button to run the next mode, or send `1` through `9` over serial to select a mode directly.
6. Press either side button for an immediate hard abort.

Modes never advance automatically. Every mode is bounded by a frame count and a 3.5-second refresh timeout. A timeout also resets and halts the panel.

## Test matrix

| Mode | Waveform | PLL | Updated area |
|---:|---|---:|---|
| 1 | Built-in OTP DU | `0x0E` | 800×480 |
| 2 | Built-in OTP DU | `0x0F` | 800×480 |
| 3 | Built-in OTP DU | `0x0F` | 800×160 |
| 4 | Recovered factory `UC8279_aa_prebw_mid` external LUT | `0x0F` | 800×160 |
| 5 | Generated A2, 8 scan frames | `0x0F` | 800×160 |
| 6 | Generated A2, 4 scan frames | `0x0F` | 800×160 |
| 7 | Generated A2, 2 scan frames | `0x0F` | 800×160 |
| 8 | Generated A2, 1 scan frame | `0x0F` | 800×160 |
| 9 | Generated A2, 4 scan frames | `0x0F` | 800×480 |

The generated A2 tables leave unchanged-white and unchanged-black cells at VSS and drive only white→black and black→white transitions. No panel voltage, VCOM, booster, or PMIC register is changed. The experiment varies only SPI transport, partial-window geometry, PLL selection, and external LUT phase duration.

## Telemetry

Each frame logs:

- DTM2/new-plane upload time;
- BUSY_N waveform time;
- DTM1/old-plane synchronization time;
- total frame time; and
- instantaneous FPS.

Each completed mode prints averages for the same stages. The moving checkerboard is generated procedurally, so rendering and SD I/O are absent from the timing.

## Build

```sh
python -m pip install platformio==6.1.19
pio run -c platformio.x4lab.ini -e x4-high-fps-lab
```

Outputs:

- `.pio/build/x4-high-fps-lab/firmware.bin` — application image, offset `0x10000`
- `.pio/build/x4-high-fps-lab/firmware-merged.bin` — complete flash image, offset `0x0`
- `.pio/build/x4-high-fps-lab/firmware.elf` — symbols/debugging
