# X4 Pro high-FPS panel laboratory

Standalone firmware for experimentally driving the **UC8279 ZHX** Xteink X4 Pro panel outside the Reader runtime. It does not mount the SD card, load RiscRTE drivers, start touch, or enter the normal application firmware.

## Physical abort

**Press either side navigation button at any time.** Both buttons are connected to an IRAM GPIO interrupt. The interrupt immediately:

1. pulls panel `RST` (GPIO14) LOW;
2. forces panel `CS` (GPIO13) HIGH;
3. stops the panel clock/data/DC outputs;
4. latches an abort flag; and
5. leaves the firmware permanently halted with reset held LOW until the device is rebooted.

The v0.1.5 transport uses transactions of up to 160 rows. This does not delay the electrical abort: the interrupt raises CS and asserts panel reset independently of the polling SPI transaction. The foreground path also checks the abort between transactions and during every bounded BUSY wait.

The **power button** starts the next test mode. During a run it requests a soft stop after the current refresh. USB serial `q`/`x` is another hard-abort path.

## Controller gate

The firmware probes `FLG` (`0x71`) and `VER` (`0x70`) twice before enabling hardware SPI. It refuses to run unless both reads agree, BUSY_N reports idle, and `VER[2]` is **0x68 or 0x69**. Those are the ZHX UC8279 LUT IDs this experiment targets. Other controllers, ambiguous reads, and floating-bus results halt with panel reset held LOW.

## Operation

1. Flash the merged artifact at offset `0x0`.
2. Open USB serial at 115200 baud.
3. Reboot without holding the left button; GPIO0 is also a boot strap.
4. The firmware performs one OTP full-clean baseline and waits.
5. Press the power button to run the next mode, or send `1` through `9` over serial to select a mode directly.
6. Press either side button for an immediate hard abort.

Modes never advance automatically. Every mode is bounded by a frame count and a 3.5-second refresh timeout. A timeout also resets and halts the panel.

## v0.1.5 test matrix

| Mode | Waveform/state model | SPI | Updated area | Purpose |
|---:|---|---:|---|---|
| 1 | 2-frame Bayer, differential DTM1/DTM2 | 20 MHz | 800×160 | Known-good reference |
| 2 | 1-frame Bayer, differential DTM1/DTM2 | 20 MHz | 800×160 | Halve waveform duration |
| 3 | 2-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×160 | Isolate sync-removal gain |
| 4 | 1-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×160 | Combined waveform + sync gain |
| 5 | 2-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×80 | Halve gate window |
| 6 | 1-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×80 | Primary second-doubling target |
| 7 | 1-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×40 | Maximum-rate narrow-band test |
| 8 | Same as mode 6 | **40 MHz** | 800×80 | Isolated SPI-overclock A/B |
| 9 | 1-frame Bayer, absolute target drive; no DTM1 sync | 20 MHz | 800×480 | Full-visible-panel stress test |

Modes 3–9 use an **absolute** LUT: both possible old-state entries for a new white pixel drive toward white, and both possible old-state entries for a new black pixel drive toward black. This makes DTM1 irrelevant and removes the post-refresh old-plane upload. Unlike differential A2, unchanged pixels are driven again on every frame, so watch for contrast pumping, background darkening, edge bloom, or cumulative charge artifacts.

Mode 8 is the only 40 MHz SPI mode. All other modes remain at the previously tested 20 MHz rate. No mode changes panel voltage, VCOM, booster voltage, source/gate voltage, PMIC configuration, or battery settings.

## Telemetry

Each frame logs:

- DTM2/new-plane upload time;
- BUSY_N waveform time;
- DTM1 synchronization time (`0` in absolute modes);
- total frame time; and
- instantaneous FPS.

Each completed mode prints averages for the same stages. Frames are precomputed before the timed loop, so rendering and SD I/O are absent from the measured frame interval.

## Build

```sh
python -m pip install platformio==6.1.19
pio run -c platformio.x4lab.ini -e x4-high-fps-lab
```

Outputs:

- `.pio/build/x4-high-fps-lab/firmware.bin` — application image, offset `0x10000`
- `.pio/build/x4-high-fps-lab/firmware-merged.bin` — complete flash image, offset `0x0`
- `.pio/build/x4-high-fps-lab/firmware.elf` — symbols/debugging
