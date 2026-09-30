> Historical experiment, retired in 1.1.20. Its model, trainer, tests and benchmark were removed. Commands below describe the historical implementation; see [production renderer](HOLLOW_TRAIL_RENDER_TESTS.md) for current behavior.

# Hollow Trail learned dither experiment — 1.1.12

The app provides exactly **AI** and **AI + Dither**, selected with B/Up while paused. AI is the default. Both use the existing trained scene compositor. The per-pixel final neural 960x540 upscale path from #295 is removed, following the owner's device slowdown report. The panel still receives 960x540 output.

## Learned mapping

The student is a categorical lookup model trained offline, not a runtime neural network. Four neighbouring intensities, each quantized into eight bins, select one of 4096 learned classes. Each class contains three output-dot decisions for each of sixteen fixed Bayer phases. The original anchor dot remains exact. Local ranges below 80 tones retain the original packed interpolation/dither path. A conservative four-lane upper-bit check skips scalar edge tests when every neighbourhood is confined to a 64-tone interval.

Training uses 524,288 synthetic patches: randomly populated intensity bins with perturbed local slope extensions and some independent neighbours. No level, camera, boat, geometry or scene IDs enter the model. The offline teacher is the existing INT8 residual reconstruction followed by the exact output thresholds; majority-vote labels minimize dot disagreement within each class/phase. Validation uses 131,072 independently seeded patches. This distils the prior neural output into direct bit decisions; it does not establish better perceptual quality than the teacher or the AI baseline.

The 32 KiB expanded mapping contains only 332 distinct phase patterns. Lossless deduplication reduces deployed constants to **10,848 bytes**, with no per-frame allocation or full-size grayscale buffer. Training provenance, hashes and held-out error are in `HOLLOW_TRAIL_DITHER_MODEL.json`. Runtime has table reads and bit operations, with no final-output neural multiply-accumulates. Background neural reconstruction remains unchanged.

## Measurements and limits

`HOLLOW_TRAIL_DITHER_BENCH.csv` records one host run, `cc -std=c11 -Os`, 30 views across all ten chapters, 120 packs per mode/view with alternating order. Both packers receive identical frozen scenes. A further 360 moving-camera frames exercise changing inputs.

- AI packing: mean **105.948 microseconds**.
- AI + Dither packing: mean **176.248 microseconds** (approximately **66% more packing CPU time**).
- Static output differs from AI by 315.9 dots on average out of 518,400 (0.061%); worst sampled moving-frame disagreement is 891 dots (0.172%).
- Mean absolute difference of 8x8 block-average tones: 0.120 on a 0–255 scale. These are agreement metrics, not perceptual quality scores.
- Learned output produced approximately 0.53% more bit flips over the moving traces than AI. Fixed phase and repeatability prevent injected random noise, but do not guarantee freedom from shimmer as inputs cross learned bins.

An earlier comparison with the now-removed #295 neural output measured roughly 23–25% less packing CPU time using the student. That comparison is not a gain over AI. Device FPS, PSRAM lookup costs, display behavior and subjective clarity remain unmeasured. This is deliberately an optional experiment, not a claimed performance improvement.

## Validation and reproduction

`hollow_trail_dither_test.c` checks every constant tone against original output, a scalar physical-pixel reference, randomized/high-contrast/gradient patterns, clipping, aligned and unaligned output, padded stride, guards, deterministic output, and cooperative servicing. The gameplay test verifies the two output selections with AI composition enabled. Both pass AddressSanitizer/UndefinedBehaviorSanitizer (host LeakSanitizer disabled). The actual ESP32-S3 app build and ELF structural validation pass. App version: **1.1.11 -> 1.1.12**; no newer firmware requirement.

Train using `python3 scripts/train_hollow_trail_dither.py` (NumPy, offline only). Compile the benchmark with `cc -std=c11 -Os -IApps -Ilib/NativeApps/include -Isdk/driver test/native_apps/hollow_trail_dither_bench.c -o /tmp/hollow-dither-bench`, then run it to emit CSV. The benchmark also writes two PBM example frames to /tmp.
