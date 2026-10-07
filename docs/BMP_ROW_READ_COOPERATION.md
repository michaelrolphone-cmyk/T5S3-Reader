# BMP row-read cooperation

Report: [PERF-20261005-BMP-ROW-IO-WAITS](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6002109660).
Repair claim: [6010753360](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6010753360).

Source/integration baseline is PR350 `xteink-x4-pro-boot` at
`f14196766af6c3bc370d1755d7d523a5bad5650d`. Its complete Bitmap sources and both
renderer bitmap methods are unchanged from the report's `b91bc323` baseline.
Default master is `21ce3b5b720e106815c8f3a7778bc7003294e0e2`; this repair is for
PR350 and is not a claim of master integration. Firmware advances **1.3.154 →
1.3.155**. No native-app, driver or provider package payload changes.

## Focused change

`GfxRenderer::drawBitmap` and `drawBitmap1Bit` opt X4/T5 rendering into the existing
`HalReadBudget` mechanism. Each rendering pass owns one fixed-size stack budget.
`Bitmap::readNextRow` accepts an optional budget; default callers still use the
original read. Other renderer targets pass null and retain ordinary reads.

Every row still makes the same `rowBytes` request at the same file offset. The
unchanged HAL performs the same bounded provider transfers, partial-positive-read
completion, lock/media/error checks and 20-second per-call deadline. No read-ahead,
image cache, read batching, parser change, driver change or global wait suppression
is introduced. Header reads, palette/dithering, pixel values, clipping/scaling,
three grayscale passes, row buffers, error return boundaries, rewind and cleanup
remain unchanged.

The existing budget yields with `vTaskDelay(1)` after 4 KiB, 32 reads/rows, or an
8 ms elapsed-time checkpoint. Conversion work is checked after each decoded row;
this also covers rows skipped by crop/clipping. The next read's pre-checkpoint
accounts for the preceding renderer CPU work. A final checkpoint covers the last
rendered row and an early clipping break. Failure exits retain the HAL's pre/post
checkpoints. A budget never survives the rendering call or an early exit.

This is **8 ms plus at most one bounded row/provider operation**, not a hard 8 ms
latency guarantee. The existing maximum width is 2,048 pixels, maximum height
3,072, and maximum supported row is 8,192 bytes. Scheduling necessarily changes
wall-clock interleavings; no universal race/deadline-edge equivalence or physical
speedup is claimed. Timeout policies, requests and recovery decisions are not
relaxed.

## Deterministic costs

These are requested waits in a production-source host model, not SD sector
counts or physical device time. The complete Bitmap parser/helpers and both
renderer bitmap loops execute with the production volume-HAL read implementation.

| BMP size/depth | Passes | Pixel reads before/after | Pixel waits before | Pixel waits after |
| --- | ---: | ---: | ---: | ---: |
| 240 × 400 / 2-bpp | 3 | 1,200 | 1,200 | 36 |
| 240 × 400 / 4-bpp | 3 | 1,200 | 1,200 | 36 |
| 480 × 800 / 1-bpp | 1 | 800 | 800 | 25 |
| 480 × 800 / 2-bpp | 3 | 2,400 | 2,400 | 75 |
| 480 × 800 / 4-bpp | 3 | 2,400 | 2,400 | 132 |
| 540 × 960 / 2-bpp | 3 | 2,880 | 2,880 | 90 |
| 540 × 960 / 4-bpp | 3 | 2,880 | 2,880 | 180 |

The 480 × 800 four-gray cases retain all **34 header waits** and exactly 288,000
(2-bpp) or 576,000 (4-bpp) pixel bytes. Wide 8,192-byte rows still yield for each
4 KiB provider chunk; this optimization does not promise fewer waits for every
possible row size.

## Regression and review

`test/bmp_row_reads/run_test.py` compiles the full production Bitmap and helpers,
extracts the two complete renderer methods and complete HAL read methods, and
links explicit storage-provider, clock, allocation and pixel-sink boundaries.
The fake provider and pixel sink are not hardware, FatFs or panel measurements.

The 108 render cases cover all supported depths (1/2/4/8/24/32), both row orders,
three representative image sizes, all three planes, high-color dithering,
non-multiple-of-four packed tails, 1×1 and maximum-width rows, scaling, crop,
negative/offscreen coordinates and font-scan suppression. Fault cases include
truncation, provider errors, short positive reads, unavailable media, lock failure,
both row-allocation failures, mid-read media loss, slow provider time, deadline
expiry, slow renderer CPU and 32-bit clock wrap. Separate controls cover default
and opt-in failed-read retry, rewind failure/retry and zero-progress cooperation.
Every case checks buffer/handle cleanup; output records include full-frame and
ordered provider/seek trace hashes, request/byte counts, final offset, sticky error,
allocation count and largest row allocation.

Independent exact-baseline, optimized normal and fatal ASan/UBSan records match
byte-for-byte, SHA256:
`a1040381e2008b0be81481f6261aefa35b3185bcaddf5a0778b5e2acd41c5780`.
The unchanged original independently fails the optimized wait assertion. The
legacy/null-budget guard retains original waits. Independent review also passes
X4/T5/legacy sanitizer runs and the existing BMP-layout regression.

Run from the repository root:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  python3 test/bmp_row_reads/run_test.py --sanitize
python3 test/bmp_row_reads/run_test.py --t5
python3 test/bmp_row_reads/run_test.py --legacy
# Supply an independent checkout at the exact baseline for the original:
python3 test/bmp_row_reads/run_test.py --source-root /path/to/f1419676 --baseline
# The same original without --baseline must fail the optimized wait assertion.
```

The no-argument native aggregate runs X4/T5 sanitizers plus the legacy guard.
Per-artifact ELF validation still exits before aggregate tests. Local LeakSanitizer
is unavailable under tracing; ASan/UBSan remain active. Local full firmware builds
are unavailable because PlatformIO and the Xtensa compiler are absent; exact-head
hosted builds must be checked after publication. Hardware, panel timing and
physical SD behavior remain unrun. No merge, release, deployment or device action
is part of this repair.
