# X4 Pro UC8279 focused directional-balance laboratory v0.1.9

Standalone firmware for narrowing the remaining first-transition contrast problem on the Xteink X4 Pro after the v0.1.8 visual matrix.

The prior device run established several useful constraints:

- complementary directional **2+2** was the strongest moving witness;
- maximum kick followed by a GND hold was visible but weaker;
- VCOM offsets at the high end of the sweep darkened the complete display;
- broad simultaneous transition loads degraded white erasure toward gray;
- short source/gate timing remained active, while the combined long-TCON/17-Hsync mode produced no motion.

This matrix therefore fixes VCOM at the factory value and the documented PLL at 200 Hz, updates only the actual horizontal changed region, and isolates pulse asymmetry, source-rail direction, transition-load segmentation, split-direction refreshes, recharge gaps, TCON, and CDI.

It does not boot RiscRTE, mount storage, start touch, or load installed applications.

## Electrical and geometry boundary

All modes retain the proven laboratory foundation:

- recognized UC8279 ZHX LUT ID `0x68` or `0x69` only;
- normal 800×600 controller geometry;
- visible rows at gate offset 120;
- 20 MHz panel SPI;
- ordinary PTIN/PTL addressing;
- documented 200 Hz PLL only;
- factory VCOM only;
- bounded BUSY assertion and completion waits;
- a stock OTP baseline before every candidate;
- no compact TRES/GSST remapping;
- no 40/80 MHz SPI;
- no undocumented PLL values;
- no PMIC or battery changes.

The source-rail modes use documented ±14 V and ±15 V settings. They are characterization experiments, not production defaults.

## Immediate abort

Either side button immediately raises EPD CS, asserts EPD RESET low, stops clock/data output, and permanently halts until reboot. USB serial `q`, `x`, or Escape invokes the same path.

The power button starts the next mode. During a mode it requests a stop after the current logical frame; a segmented or split-direction logical frame may contain more than one physical DRF.

## Witness geometry

The active witness is an 800×80 band:

- upper half: black target on white;
- lower half: white target on black.

Teleport modes bounce between adjacent non-overlapping target positions. The union of the old and new positions is exactly 160 pixels wide, so the controller updates a realistic narrow changed rectangle rather than scanning all 800 columns. The geometry prevents repeated overlap from making a weak first transition appear stronger.

Density modes toggle centered 400- or 800-pixel regions. Segmented variants divide the same logical transition into narrower physical windows so supply/load effects can be compared without changing the final image.

## 25-mode matrix

### Directional pulse balance

| Mode | Key | Experiment |
|---:|:---:|---|
| 1 | `1` | Complementary 2 black phases + 2 white phases, ±14 V reference |
| 2 | `2` | 1 black + 2 white phases, ±14 V |
| 3 | `3` | 1 black + 3 white phases, ±14 V |
| 4 | `4` | 1 black + 4 white phases, ±14 V |
| 5 | `5` | 2 black + 3 white phases, ±14 V |
| 6 | `6` | 2 black + 4 white phases, ±14 V |
| 7 | `7` | Prior maximum-kick + symmetric GND-hold reference, ±14 V |
| 8 | `8` | 1+2 plus one additional black→white GND-hold phase, ±14 V |

Modes 2–6 determine whether the best v0.1.8 result can be made faster by spending fewer phases on white→black and more on the weaker black→white direction.

### Directional source-rail contribution

| Mode | Key | Experiment |
|---:|:---:|---|
| 9 | `9` | 1+2, VDH +15 V / VDL −14 V |
| 10 | `a` | 1+2, VDH +14 V / VDL −15 V |
| 11 | `b` | 1+2, symmetric ±15 V |
| 12 | `c` | 1+3, symmetric ±15 V |
| 13 | `d` | 2+2, symmetric ±15 V |

Compare the upper and lower witnesses separately. These modes determine which rail strengthens the weak black→white transition and whether added voltage is more useful than another phase.

### Transition-density segmentation

| Mode | Key | Experiment |
|---:|:---:|---|
| 14 | `e` | 400-pixel transition in one combined window, 1+2 |
| 15 | `f` | Same 400-pixel transition as two 200-pixel windows, 1+2 |
| 16 | `g` | 800-pixel transition in one combined window, 1+2 |
| 17 | `h` | Same 800-pixel transition as two 400-pixel windows, 1+2 |
| 18 | `i` | Same 800-pixel transition as four 200-pixel windows, 1+2 |
| 19 | `j` | Four 200-pixel windows with a 2 ms recharge gap between segments |

The final logical image is identical within each comparison. Better contrast after segmentation would support a high-voltage rail/source-driver loading explanation.

### Separate directional DRFs and recharge

| Mode | Key | Experiment |
|---:|:---:|---|
| 20 | `k` | 400-pixel transition; white→black and black→white in separate DRFs, no gap |
| 21 | `l` | Same split-direction sequence with a 3 ms recharge gap |

These determine whether simultaneous opposite-direction source loading is suppressing white erasure and whether a short booster-recharge interval helps.

### Independent timing variables

| Mode | Key | Experiment |
|---:|:---:|---|
| 22 | `m` | Default TCON with short 2-Hsync CDI |
| 23 | `n` | Minimum TCON with default CDI |
| 24 | `o` | Default TCON with long 17-Hsync CDI |
| 25 | `p` | Long TCON with default CDI |

The prior failed timing mode changed TCON and CDI together. These four modes isolate each variable.

## What to record

For every mode, note:

1. **Black first appearance:** darkness at the first new location.
2. **White first erasure:** how fully black becomes white on the first update.
3. **Old-location residue:** whether the previous black or white target remains.
4. **Motion completeness:** whether every target position is visibly rendered.
5. **Directional balance:** relative strength of white→black and black→white.
6. **Background stability:** whether unchanged white or black shifts or becomes dusty.
7. **Post-run relaxation:** change in the retained image before another mode begins.
8. **Cadence:** whether added contrast is worth the visible reduction in motion rate.

For segmented modes, inspect seams at segment boundaries as well as contrast. For split-direction modes, note whether the two halves appear temporally separated.

A compact report format is:

```text
Mode:
Black first frame:
White first frame:
Old-location residue:
Motion completeness:
Background/seams:
Post-run relaxation:
Overall rank:
```

## Suggested order

```text
1 → 2 → 3 → 4 → 5 → 6 → 7 → 8
2 → 9 → 10 → 11 → 12 → 13
14 → 15
16 → 17 → 18 → 19
20 → 21
22 → 23 → 24 → 25
```

This starts from the known 2+2 winner, narrows the asymmetric pulse knee, then evaluates voltage, loading, segmentation, split-direction execution, and finally timing.

## Controls

- **POWER while ready:** run the next mode.
- **POWER during a mode:** stop after the current logical frame.
- **Serial `n` or Space:** run the next mode.
- **Serial `1`–`9`, `a`–`p`:** select a mode directly.
- **LEFT or RIGHT side button:** immediate electrical hard abort.
- **Serial `q`, `x`, or Escape:** immediate hard abort.
- **Serial speed:** 115200 baud.

Modes never auto-advance. The final image remains on the glass for inspection until another mode begins.

## Build and flash

```bash
pio run -c platformio.x4lab.ini -e x4-high-fps-lab
```

Flash `firmware-merged.bin` at offset `0x0`.
