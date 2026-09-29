# Hollow Trail fused SIMD comparison — 1.1.13

The app now offers exactly **AI** (default) and **AI + SIMD**. Pause and press **B/Up** to alternate. Both use the established AI scene compositor and the same interpolation/ordered-dither output. The learned dither correction and the removed neural 960 upscale are not used by either selectable mode. The owner clarified that plain AI was generally faster than AI + Dither, despite occasional roughly 0.2 FPS gains in particular views.

## Kernel

`hollow_trail_simd.inc` implements an ESP32-S3 PIE assembly leaf operating on 16 logical pixels at a time, generating two rows of 32 physical dots. It processes contiguous blocks within one bounded row span per call. Aligned 128-bit loads and byte shifts construct neighboring samples. Four exact floor averages, signed-byte threshold comparisons and register bit gathering produce two packed 32-bit stores. No full-size intermediate, 16-bit conversion raster, neural inference, allocation or additional grayscale pass is introduced.

Unsigned pixels are biased by XOR 128. The identity `floor((a+b)/2) = (a&b) + arithmetic_shift(a^b,1)` is evaluated in signed byte lanes. Its result fits a signed byte, making the saturating add exact. Explicit masks recover byte-wise arithmetic shifts from four 32-bit vector shifts. The two original diagonal rounding points and all sixteen spatial threshold phases are retained. Scalar prologue/tails handle clipped edges and unsupported alignment; the final row/column retain their original edge extension.

The kernel restores SAR, uses the windowed ABI local stack area and makes no calls with live QR intermediates. QR registers are caller-saved S3 CP3 state, covered by the platform's existing coprocessor context mechanism. No direct hardware/peripheral control or new firmware API is involved. Instruction reference: Espressif ESP32-S3 Technical Reference Manual v1.8, Processor Instruction Extensions, especially EE.SRCI.2Q, EE.VADDS.S8, EE.VSR.32 and EE.VCMP.GT.S8.

## Runtime validation and fallback

Availability requires the existing math API's S3 DSP feature flag. During bounded startup, the app compares actual assembly output against the portable model on 64 patches × four spatial phases, alternating single-block and two-block spans with guarded output. Input/yield checkpoints remain active. A mismatch or unavailable backend disables SIMD. Selecting it then displays **SIMD UNAVAILABLE: AI** and uses the AI packer; it cannot silently label fallback timing as SIMD.

The self-test runs before profiling starts. Mode changes discard pending output and reset performance counters. The usual FPS and PACK measurements provide the device comparison. No hardware speedup is claimed: the scalar packer already has strong flat-region shortcuts, and load/gather/call overhead can outweigh SIMD savings.

## Verification

- ASan/UBSan: all 65,536 byte-pair means, random/gradient/high-contrast patterns, all spatial phases, clipping, frame bounds, stride padding, unaligned output and unavailable-backend fallback passed.
- Full-frame portable SIMD-model output equals AI output across all ten chapters, including transformed/vista views. Cooperative servicing is exercised.
- Existing learned-dither reference and full gameplay tests passed. The host tests exercise the portable instruction model, **not S3 opcodes or S3 timing**.
- Actual ESP32-S3 ELF build and structural validation passed; disassembly contains the vector loads, shifts, adds and comparisons. Actual S3 execution is checked by the startup self-test on the device and remains unverified on this host.
- App manifest, emitted sidecar and catalog: **1.1.12 -> 1.1.13**; unchanged minimum firmware.

Reproduce the host differential test using the same include paths as the native test runner and `test/native_apps/hollow_trail_simd_test.c`. Build the S3 app with `python3 scripts/build_all_apps.py --id hollow_trail`.
