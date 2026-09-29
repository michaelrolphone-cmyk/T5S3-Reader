# Hollow Trail expanded SIMD — 1.1.14

The owner measured a major PACK reduction and about +2 FPS with 1.1.13. This
experiment keeps that successful packer in **both** selectable modes:

- **AI + SIMD: BASELINE** (default): the established AI renderer and SIMD packing.
- **AI + SIMD: EXPANDED**: the same packer, plus all four shared optimizations below.

Pause and press **B/Up** to alternate. The switch discards prepared output and
resets profiling. The existing FPS, RENDER, PACK, background and CACHE readings
provide the comparison. No learned dither or neural 960 option returns. No
chapter-specific optimization, new model weights or graphics change is introduced.

## Four connected operations

| Operation | Actual change | Exactness and bounds |
| --- | --- | --- |
| Scene upscale, 240×135 → 480×270 | S3 vector averages and byte interleaving write two output rows directly, 16 source pixels per block. | Preserves the nested floor averages; scalar final columns and odd reconstruction row. Input loads support the 120-byte reconstruction stride as well as 240-byte rows. |
| AI reconstruction, 120×68 → 240×135 | Reuses vector interpolation, then vector min/max, saturating subtraction and comparison produce a row's edge gate. | Saturating the range to 127 cannot change the >=80 decision. The existing neural residual inference and weights are unchanged; source anchors and all residual rounding are preserved. |
| Cached layer compositing | Register-only deinterleaving gathers every fourth pixel, then eight 16-bit lanes fuse focus blending, depth fading and maximum with the destination. | Preserves both rounding points, radial weights 0..256, fades 45/70 and negative differences. Arbitrary camera byte offsets are handled by aligned loads plus SAR_BYTE extraction. Scalar prefixes/tails and tile guards prevent out-of-row source loads. |
| Blur/cache generation | Eight-column leaf fuses output conversion and next-row running-sum updates. | Replaces separate add/sub DSP calls and scalar output conversion for the vector span. The horizontal rolling sum remains scalar. Radii 1..9 use exact corrected reciprocal division; larger radii and unsuitable alignment use the baseline. |

No full-frame staging/conversion allocation is added. Temporary edge flags occupy
128 bytes on the stack; blend/blur constants use 32 bytes each. Work remains
bounded to one row span and retains the renderer's cooperative checkpoints. Blur
still works in the existing incremental cache bands. A mode switch need not
invalidate the cache because both modes produce identical cache bytes.

## Arithmetic and memory contract

The upscale leaf uses the same biased signed-byte averaging as the successful
packer and `EE.VZIP.8` for output. The reconstruction gate compares biased signed
minimum/maximum values; signed saturation only clamps ranges already above the
threshold. A scalar tail evaluates the rightmost edge with the existing extension.

The blend leaf evaluates `near + floor((wide-near)*radial/256)`, followed by
`floor(v*(256-floor(radial*fade/256))/256)`. All intermediate lane values fit signed
16 bits after each multiply's shift. This is also exact for the old special cases
radial=0 and radial=256. Near/wide cache reads have a conservative 15-byte right
guard; the leading aligned read stays inside the aligned cache row. Weight loads
are exact eight-byte accesses and destination accesses are exact four-byte words.

For blur, `q=(sum*ceil(65536/n))>>16` is corrected by subtracting one when `q*n>sum`.
For n=3,5,…,19 and sum=0..255*n, this equals both integer division and the existing
24-bit reciprocal result. Product/comparison and intermediate running sums fit
signed 16 bits. On the final row, add and subtract point to the same valid row, so
output is written without changing the running sum. Radius zero remains a copy.

The four S3 leaves in `Apps/hollow_trail_expanded_simd.inc` preserve SAR; the
compositor also preserves SAR_BYTE (user register 13). All use only QR scratch
registers and leave the upper 16 stack bytes for the windowed ABI. Instructions
follow Espressif's ESP32-S3 Technical Reference Manual v1.8, PIE: VLD/VST, SRC,
VZIP/VUNZIP, VMUL.S16/U16 and signed comparison/saturating add/subtract.

## Runtime fallback

Both the existing packer self-test and the new expanded self-test must pass to
allow expanded mode. The expanded startup test executes all four real assembly
leaves against independent C equations over 128 deterministic patterns, both
single- and two-block loops, aligned/offset inputs, radial endpoints, both fades,
all optimized blur radii and guarded destinations/running sums. It yields every
eight patterns. The check runs before gameplay profiling, with simulation frozen.

An expanded failure retains SIMD packing and shows **EXPANDED UNAVAILABLE: BASE**.
If packing itself is unavailable, the app shows **SIMD UNAVAILABLE: AI**. Fallback
cannot silently masquerade as expanded timing. No new firmware import or minimum
firmware version is needed.

## Verification and limits

- ASan/UBSan differential checks: all near/wide byte pairs × 257 radial weights ×
  both fades; all reachable blur sums for radii 1..9; full interpolation and neural
  reconstruction; 547 camera offsets × four depths; blur boundaries/bands, radius
  fallback and fresh complete frames at three views in each of all ten chapters.
- The host tests use C leaf oracles, **not S3 execution**. Device startup tests are
  the execution gate for the actual opcodes. Host timings are not speed estimates.
- Existing packing, gameplay, controller, pipeline and cooperative-service tests
  are run alongside the targeted differential checks.
- S3 app build and native ELF structural validation; manifest, sidecar and catalog
  version/digest checks. Version **1.1.13 → 1.1.14**, unchanged minimum firmware.

Reproduce the targeted checks with the include paths in `test/run_native_app_test.sh`
and `test/native_apps/hollow_trail_expanded_simd_test.c`. Build the target with
`python3 scripts/build_all_apps.py --id hollow_trail`. This environment requires
`ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer cannot enumerate `/proc` tasks;
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

The new stages have not been timed on the device. Compare warmed gameplay in both
modes, and also movement into uncached terrain for the blur/cache change. Real FPS
and stage timings determine whether the combined expansion should become default.

## 1.1.15 availability repair

The owner reported `SIMD UNAVAILABLE: AI` on 1.1.14. This means the packing
availability check failed before expanded kernels were enabled. The original
message did not distinguish a missing math API/feature from a test mismatch, so
that device's exact failure cannot be established from the message alone.

Inspection found a concrete alignment defect: both SIMD constant tables were
`aligned(16)` objects in ELF `.rodata`. The S3 section loader in
`lib/elf_loader/src/esp_elf.c` concatenates data sections into a block obtained by
`esp_elf_malloc`; `esp_elf_adapter.c` uses ordinary `heap_caps_malloc`, without a
16-byte alignment guarantee. Source/linker alignment therefore does not guarantee
runtime alignment. S3 `EE.VLD.128` rounds addresses down to a 16-byte boundary, so a
misplaced constant table causes wrong arithmetic and a self-test rejection. This
can depend on heap/ELF layout, even when the kernel source is unchanged.

Both tables (432 bytes total) now occupy the end of the explicitly aligned app
PSRAM arena. `ht_bind` generates them once; the self-tests and production kernels
use these same pointers. They follow the reconstruction raster without overlap,
and remain alive until normal app memory cleanup. The version is **1.1.15**;
minimum firmware is unchanged. This app fix does not change the firmware loader.

The pause screen and app log now distinguish no math API, no S3 feature, table
alignment failure, packing mismatch, and upscale/edge/blend/blur mismatches.
The log includes pattern, phase, byte and expected/actual values. Every original
self-test and fallback remains enforced; nothing forces a failing kernel on.

Regression coverage checks all 16 possible raw allocation offsets, exact table
values and arena guards, plus first-mismatch diagnostic capture. The full native
suite and S3 build are rerun. Hardware confirmation remains necessary: the
alignment defect is verified in code, but the reported device's previous generic
message did not identify which check failed.
