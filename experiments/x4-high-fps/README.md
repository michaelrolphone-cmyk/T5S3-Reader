# X4 Pro UC8279 visual contrast laboratory v0.1.8

Standalone firmware for subjective, side-by-side evaluation of first-transition
contrast on the Xteink X4 Pro. It is derived from the hardware-tested X4LAB
v0.1.5 electrical and geometry path and does not boot RiscRTE.

The matrix is designed for unaided visual inspection. A fixed 800×80 witness
band simultaneously shows:

- a black target teleporting across a white background in the upper half; and
- a white target teleporting across a black background in the lower half.

The targets jump between non-overlapping positions. This prevents the apparent
contrast gain that a sliding square receives when several consecutive frames
redrive overlapping pixels. Density modes instead toggle centered rectangles of
64, 400, or 800 pixels so rail/load sensitivity can be judged with identical
scan geometry.

## Electrical boundary

Common to every mode:

- recognized UC8279 ZHX LUT ID `0x68` or `0x69` only;
- normal 800×600 controller geometry;
- visible rows at gate offset 120;
- 20 MHz panel SPI;
- ordinary PTIN/PTL windows;
- bounded BUSY assertion and completion waits;
- a stock OTP baseline before every candidate;
- no compact TRES/GSST remapping;
- no 40/80 MHz SPI;
- no undocumented PLL values;
- no PMIC or battery changes.

Some modes deliberately exercise documented source-voltage, VCOM, TCON, and
CDI controls. They are experiments, not production recommendations.

## Hard abort

Either side button immediately raises CS, asserts the EPD reset line low,
stops clock/data output, and permanently halts until reboot. USB serial `q`,
`x`, or Escape invokes the same path.

The power button starts the next mode. During a mode it requests stop after the
current physical refresh completes.

## Matrix

| Mode | Key | Experiment |
|---:|:---:|---|
| 1 | `1` | Current corrected two-frame absolute A2 reference, 200 Hz, ±14 V |
| 2 | `2` | Conventional two-frame differential A2 with VCOM-DC reference |
| 3 | `3` | Complementary directional overdrive, one frame per direction |
| 4 | `4` | Complementary directional overdrive, two frames per direction |
| 5 | `5` | Maximum directional kick followed by symetric GND hold |
| 6 | `6` | Maximum kick followed by VDHR intermediate hold |
| 7 | `7` | Reduced reverse activation followed by maximum directional kick |
| 8 | `8` | Complementary directional waveform at 100 Hz |
| 9 | `9` | Complementary directional waveform at 130 Hz |
| 10| `a`| Complementary directional waveform at 150 Hz |
| 11 | `b` | Symmetric ±12.6 V source rails |
| 12 | `c` | Symmetric ±15.0 V source rails |
| 13 | `d` | VDH +15.0 V, VDL ∋14.0 V |
| 14 | `e`| VDH +14.0 V, VDL ∋15.0 V |
| 15 | `f` | ±15 V with VCOM DC ∋1.50 V |
| 16 | `g` | ±15 V with VCOM DC ∋2.00 V |
| 17 | `h` | ±15 V with VCOM DC ∋2.50 V |
| 18 | `i` | ±15 V with VCOM DC ∋3.00 V |
| 19 | `j` | 64-pixel transition-density load |
| 20 | `k` | 400-pixel transition-density load |
| 21 | `l` | 800-pixel transition-density load |
| 22 | `m` | Minimum documented gate/source non-overlap and 2-Hsync CDI |
| 23 | `n` | Longer gate/source non-overlap and 17-Hsync CDI |
| 24 | `o` | Temporal Bayer grayscale stress using complementary drive |
| 25 | `p` | Teleporting one-pixel edge/checker stress |

Mode 3 matches the complementary two-phase architecture used by the current
panel-driver experiment. Modes 5–7 test different ways to spend more useful
field within a small number of phases rather than merely repeating the same
absolute pulse.

## What to record by eye

For every mode, note these separately:

1. **Black first appearance:** how dark the upper target is at its first new
   location rather than after several frames.
2. **White first erasure:** how cleanly the lower target becomes white at its
   first new location.
3. **Trailing residue:** whether the prior target location remains visible.
4. **Motion completeness:** whether each position is visibly rendered or the
   target appears to jump over faint intermediate positions.
5. **Direction imbalance:** whether white→black is materially stronger than
   black→white or vice versa.
6. **Background stability:** whether unchanged white or black areas
   are disturbed by the waveform.
7. **Post-run relaxation:** inspect the retained final frame before starting
   the next mode.

For modes 19–21, compare contrast as the number of simultaneously transitioning
columns rises. A contrast decline with increasing transition density would be
consistent with high-voltage rail or source-driver loading.

For modes 15–18, identify the VCOM value that best balances both transition
directions; do not judge only the darkest black.

## Controls

- **POWER while ready:** run the next mode.
- **POWER during a mode:** stop after the current physical refresh.
- **Serial `n` or Space:** run the next mode.
- **Serial `1`–`9`, `a`–`p`:** select a mode.
- **LEFT or RIGHT:** immediate electrical hard abort.
- **Serial `q`, `x`, or Escape:** immediate hard abort.

Serial speed is 115200 baud. Modes never auto-advance. The final image remains
on the glass for inspection until another mode is started.

## Build and flash

```bash
pio run -c platformio.x4lab.ini -e x4-high-fps-lab
```

Flash `firmware-merged.bin` at offset `0x0`.
