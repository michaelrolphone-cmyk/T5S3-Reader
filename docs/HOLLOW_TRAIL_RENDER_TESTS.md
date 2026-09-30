# Hollow Trail independent render tests — 1.1.18

The owner measured only about +0.2 FPS for the combined expansion. This build
isolates its operations to expose individual gains or regressions, and adds
shared-renderer experiments. All modes keep AI composition and the proven SIMD
packer when its device self-test passes. No learned dither, neural 960 mode,
scene-specific shortcut, effect removal or model-weight change is introduced.

Pause and press **B/Up** to cycle. The pause screen shows the numbered experiment.
Each test changes only the named operation relative to the packing baseline,
except the explicitly named combined comparisons:

| Test | Selected change |
| --- | --- |
| 1/25 Packing baseline | AI with the existing SIMD packer; all extra operations disabled. |
| 2/25 SIMD upscale | Vector 240×135 → 480×270 interpolation only. |
| 3/25 SIMD reconstruction | Vector 120×68 reconstruction interpolation and edge gate only; residual inference unchanged. |
| 4/25 SIMD compositing | Vector focus/depth blending only. |
| 5/25 SIMD blur / cache | Fused vector vertical blur output and running sums only. |
| 6/25 Packed camera | Two horizontal interpolation rows in independent 16-bit lanes of one 32-bit word. Original vertical interpolation/rounding retained. |
| 7/25 Packed vignette | Symmetric fade pixels multiplied in two 16-bit lanes, with exact division by 255 using shifts/adds. |
| 8/25 Triangle stepping | Exact integer quotient/remainder edge stepping replaces two divisions per scanline in shared triangle rasterization. |
| 9/25 All four SIMD | Previous expanded behavior: tests 2–5 together. The additional CPU experiments stay off. |
| 10/25 Bounded focus math | Clamp distance first; compute the remaining /250 through an exact bounded 32-bit reciprocal. |
| 11/25 Flat camera skip | Skip interpolation only when all four source samples are identical. |
| 12/25 Neural patch cache | Reuse an existing model result only for an identical nine-byte input patch. |
| 13/25 Blur interior | Peel clamped edges from the horizontal running-sum loop; directly address interior samples. |
| 14/25 Compositor tile plan | Calculate horizontal tile spans once per composition and reuse them for all rows. |
| 15/25 Internal neural | Stage unchanged weights, hidden values, three rolling source rows and two output rows in internal SRAM. |
| 16/25 Internal blur | Stage horizontal input and vertical add/subtract/output rows in internal SRAM; existing sums remain on the stack. |
| 17/25 Internal packing | Stage packing constants, two rolling source rows and two packed output rows in internal SRAM. |
| 18/25 Low background camera | Transform background at 240×135, with full-resolution foreground projection. Background filtering differs from baseline. Coarse terrain occlusion and fill projection are off. |
| 19/25 Coarse occlusion | Skip background composition and neural work safely hidden by large opaque terrain interiors. Original full-resolution camera retained. |
| 20/25 Solid fill camera | Replace full-resolution camera interpolation inside verified constant-color regions with direct fill runs. Original background pipeline retained. |
| 21/25 Stationary - mode 9 | Modes 2 + 4: SIMD upscale and compositing. |
| 22/25 Stationary + mode 9 | Modes 2 + 4 + 9: all four SIMD stages, identical to mode 9. |
| 23/25 Motion - mode 9 | Modes 2 + 4 + 7 + 14: upscale, compositing, packed vignette and tile planning. |
| 24/25 Motion + mode 9 | Modes 2 + 4 + 7 + 9 + 14: all four SIMD stages, packed vignette and tile planning. |
| 25/25 All combined | Every optimization flag from modes 2–20, including low background camera, occlusion, fill projection and all three internal workspaces. |

Mode 25 composes flat-sample rejection with packed interpolation for low and
foreground camera sampling, and projects solid fills inside the split camera's
foreground spans. Tile planning and SIMD blending respect coarse occlusion.
Neural cache misses use internal weights when available. Blur, neural and packing
reuse the same 4 KB workspace sequentially, reinitializing their own data.
No new memory allocation or firmware API is introduced. The mode inherits mode
18's background filtering change; compare its image against mode 18, not exact
baseline pixels. Failed SIMD stages or unavailable internal memory fall back
individually. Mode 1 remains the default; pause → B/Up cycles all 25 modes and
resets the ten-second FPS window. App **1.1.17 → 1.1.18**.

The owner reports stationary gains in 2, 4 and 9, and moving gains in 2, 4, 7,
9 and 14. These four combinations are new hardware comparisons, not measured
combined speedups. Mode 9 already contains modes 2 and 4; each stage runs once.
“Stationary” and “Motion” label candidate sets, not automatic movement detection.
Both remain enabled when moving or standing still. Modes 1–20 retain their numbers
and the default remains mode 1. Tile planning now honors the SIMD compositing
readiness bit in combined modes, with the same row bounds and scalar tails.
App version **1.1.16 → 1.1.17**; minimum firmware unchanged.

Changing tests discards prepared output and clears timing statistics. Tests 2–5
have separate startup self-tests/readiness bits; failure in one does not disable
another. A combined mode may use only the passing stages, and is explicitly
marked as falling back. No failed stage is silently enabled. CPU experiments do
not require the expanded SIMD feature gate. The original packing fallback and
failure diagnostics remain active.

## Rolling ten-second FPS

`FPS10S` counts completed gameplay frames whose completion timestamps are within
the latest 10,000 ms. While warming up, the denominator is the time since the
current measurement started. The adjacent duration, e.g. `(3.2S)` or `(10.0S)`,
shows whether the full window is available. This is a sliding window, not a batch
average reset every ten seconds or an average of rounded one-second readings.

Mode changes and resuming from pause/journal start a new window. Paused/journal
frames and idle pause time do not contaminate the next gameplay comparison. The
256-timestamp fixed ring covers the maximum frame rate allowed by the existing
42 ms frame-start interval; it uses about 1 KB, with no per-frame allocation.
Millisecond wraparound is handled with unsigned elapsed arithmetic. Other stage
averages and SCANS retain their existing short reporting windows; only FPS is
changed to the requested rolling window.

For comparison, use the same level, route, movement and camera state, then collect
at least ten seconds after resuming. Blur mainly affects cache generation: walk
into uncached terrain or reload the same level under each test and inspect CACHE
as well as FPS. A stationary warm view cannot measure a cache-building speedup.
Triangle stepping affects both cache geometry and live geometry. Camera sampling
matters when the camera transform is active. The UI exposes existing BG, WORLD,
CAMERA/EDGE, CACHE, PACK and display timing to locate the remaining cost; it does
not automatically attribute all FPS variation to the selected operation.

## Exact arithmetic and scope

Camera horizontal lanes reach at most 4080, avoiding cross-lane carry. Vignette
lanes including the rounding bias reach at most 65152, for which
`(n+1+(n>>8))>>8` equals `n/255`. Triangle stepping accumulates a nonnegative
remainder for each absolute slope and reapplies its sign; this preserves C's
truncation toward zero, including clipped starts, flat edges and reversed slopes.
Modes 1–17 operate in common renderer functions and preserve the
existing raster size, filters, model, geometry, camera trajectory and checkpoints.

## Validation

- ASan/UBSan: mode isolation/readiness combinations, all 256 camera fractional
  combinations over 20,000 random patches, every reachable vignette quotient,
  3,000 triangles including clipping/flat edges/projected vistas, and 30 fresh
  scene views across all ten chapters in the nineteen exact modes match baseline pixels.
  Mode 18 has separate foreground and conservative-culling checks.
- FPS tests: partial/full windows, transition from 10 to 20 FPS, stalled frames,
  reset/pause separation, maximum app cadence and millisecond wraparound.
- Full native suite includes the new test, the existing expanded/packing tests,
  C++ math API checks, gameplay, cache, controls and cooperative-service tests.
- Target ESP32-S3 app build and ELF structural validation; source/sidecar/catalog
  version and ELF digest agreement. App **1.1.15 → 1.1.16**, minimum firmware
  unchanged. No firmware change is required.

Host checks exercise portable SIMD oracles; actual vector execution remains gated
by device self-tests. No additional hardware FPS improvement is claimed. Build
with `python3 scripts/build_all_apps.py --id hollow_trail`; the test runner is
`bash test/run_native_app_test.sh`. In this sandbox LeakSanitizer cannot enumerate
process tasks, so local sanitizer runs use `ASAN_OPTIONS=detect_leaks=0`; address
and undefined-behavior checks stay enabled.

## Further math analysis (same unreleased PR)

The focus numerator after clamping is 0..51199. Factoring 200 as 8×25 allows
`(((distance-2500)>>3)*5243)>>17`, with a product below 2^32 and exactly the same
quotient on this interval. The test exhaustively checks distances -10000 through
1000000, including both clamp boundaries. No approximate radius/focus map is used.

Flat-camera mode reads the same four taps. Equal taps return that byte directly;
other taps execute the original interpolation and rounding. This is independent
of the earlier packed-camera experiment, so their benefits can be compared.

The neural experiment uses 256 direct-mapped entries (4 KB), a hash, and a full
nine-byte comparison before reusing three residuals. Collisions run unchanged
inference and replace the entry. The cache resets at app bind and mode changes;
results are otherwise safe across frames/chapters because inference is a pure
function of the patch and unchanged weights. Mode changes also invalidate focus
maps so the selected focus implementation actually executes.

A host workload spanning all ten chapters, three starting views and four small
camera steps per view produced 38,899 model calls. A 32-entry cache hit 2,866;
256 entries hit 4,938 (12.7%); 1024 entries hit 7,301. The selected 4 KB candidate
is therefore a **low-reuse experiment**, not an asserted performance improvement:
lookup overhead may outweigh avoided inference. These are reuse counts, not
ESP32-S3 timings. Larger cache allocation was not selected just to increase hits.

The extended sanitizer test compares 30,000 patches and immediate repeat hits
against direct inference (including eviction/hash collisions), then compares
complete fresh renders in the nineteen exact modes across every chapter. Rolling FPS
behavior and the baseline remain unchanged. Version stays 1.1.16 because this is
an update to the same open, unreleased PR rather than a new release lineage.


## Shared loop work removal

Blur-interior mode retains exact edge extension and averaging, including narrow
rectangles and the rightmost running-sum update. Only the interior eliminates
per-pixel minimum/maximum calculations. Checkpoints stay at the original rows.
This is separate from the SIMD vertical-blur experiment.

Compositor-tile-plan mode computes tile keys, cache slots, columns and span lengths
once per call, rather than repeating them on each of 64 rows. The bounded plan
uses three 16-byte records on the stack at the current raster size; its capacity
is derived from raster and tile widths. It retains scalar blend rounding, focus
weights, max composition, clipping and row checkpoints. It stores no pointers or
state between frames and handles negative tile keys and all sample alignments.

Additional ASan/UBSan differential checks cover 600 horizontal-blur rectangles
with radii 1–9, edges, one-pixel widths and full widths; plus 2,049 compositor
offsets over randomized cache/scene pixels, all depth layers and varying focus.
The full-scene comparison includes both new modes. These are independent timing
candidates; neither has a measured hardware FPS gain yet.


## Internal SRAM workspace experiments

Modes 15–17 isolate memory placement relative to the same packing baseline; they
do not enable other experiments. One optional 4,111-byte allocation provides a
4,096-byte usable workspace plus 15 bytes for runtime alignment. A union shares
that storage across the three synchronous stages. Allocation occurs once after
display startup (and input/provider acquisition), so required display resources
get first claim on memory. There is no per-frame allocation, retry or internal
fallback for bulk storage. Cleanup clears the workspace pointer and frees the
original allocation, including the app's failure/exit cleanup path.

The existing tracked `heap_caps_malloc` import requests `MALLOC_CAP_INTERNAL |
MALLOC_CAP_8BIT` explicitly; ordinary `malloc` could route this size to PSRAM.
No firmware changes or Wi-Fi lifecycle changes are required. Failure leaves all
three modes selectable but marked `BASE FALLBACK: INTERNAL RAM UNAVAILABLE`.
Startup also logs whether the workspace exists. All bulk rasters, scenery tiles,
focus maps and frame staging remain in their existing PSRAM arena.

Neural mode copies the original model weights/biases once per reconstruction and
uses unchanged integer inference. A three-row ring loads each required source
row once during the residual pass; paired output rows are copied in and back.
The initial bilinear pass is unchanged. Packing mode similarly keeps a two-row
source ring, copies its 384-byte SIMD constant table once per pack call and
writes completed output rows back. Runtime-aligned constants avoid relying on
ELF section alignment. The existing device SIMD self-test remains mandatory;
failed SIMD packing uses the existing scalar packer.

Blur mode copies each horizontal source row and each vertical add/subtract row
into internal memory, then copies the selected output interval back. The 960-byte
running-sum array was already on the stack, so it is not advertised as reclaimed
PSRAM traffic. This candidate has substantial copying overhead and may lose;
compare CACHE and FPS while entering uncached terrain. Neural and packing tests
also include staging costs in their normal measured render/packing time.

Validation covers allocation size/capability flags, failure fallback, all 16
allocation alignments, 30,000 exact neural patches, random full reconstruction,
160 blur rectangles with radii 0–9, framed/unframed packing with unaligned and
padded destinations, SIMD failure fallback, matching cooperative checkpoints,
and complete renders across all ten chapters with the workspace attached.
Host allocation tests emulate the allocator contract; physical SRAM placement
is enforced by the S3 allocator capability request. Device FPS gains remain
unmeasured. App version remains 1.1.16 in this cumulative unreleased PR.


## Low background camera, coarse occlusion and fill projection

Mode 18 moves background camera interpolation to 240×135, then upscales the
result. It changes background filtering and is not pixel-identical.
Modes 18, 19 and 20 are independent: each enables only its named approach
relative to mode 1. They do not accumulate as you cycle. The original full-resolution untransformed background is still
upscaled once into the foreground source because mist/halos and soft foreground
edges read it. Foreground writes are tracked as conservative source-row spans.
Those spans are projected using the inverse camera matrix with a bilinear support
halo and rounding guards. Only their destination spans require the original
full-resolution sampling. Untouched regions use the transformed low background.
The camera center remains (240,135), including the half-pixel low-resolution Y
center; phase, scale, vista, physics and UI remain unchanged.

Mode 19 coarse background occlusion reuses platform pieces and their real terrain
surface, bottom and cliff inset, matching the drawn geometry. It marks only
complete 16×16 blocks inside guaranteed solid bodies. A one-block erosion keeps
reconstruction and interpolation dependencies away from boundaries. The AI
compositor skips hidden samples, and reconstruction skips their neural gates.
This applies to all chapters using these terrain primitives; it does not assume
that a collision rectangle alone is an opaque wall. Gaps, foliage, complex
silhouettes and blended effects are not used as occluders. Shared scenery cache
building/blur remains intact so future camera positions have complete tiles.

Mode 18 camera projection also culls low-resolution samples whose entire 3×3 upscaled
consumer neighborhood will be replaced by foreground projection, and avoids
upscaling groups wholly replaced on both output rows. Full projected coverage
skips the low-camera pass and second upscale altogether, for any scene. The
existing fully opaque grotto background rejection is retained.

Mode 20 retains the original full-resolution camera and classifies its completed
source image into
constant-color 16×16 blocks. Adjacent blocks of the same shade form wider runs.
For each projected row, the renderer advances to where the four-tap footprint
would leave that constant region and fills the safe run directly. Only boundary
and nonuniform runs use bilinear interpolation. This is exact for black, white
and gray; a thin line or window makes its block nonuniform. Classification uses
actual final pixels, not optimistic collision coverage. Fine detail is preserved.
For mode 18, raster primitives report writes through `ht_camera_touch`; the direct mist and
grotto row writers also report coverage. Any future direct raster writer in the
foreground phase must report its touched span, while new scenes built from the
shared primitives inherit tracking automatically.

Fixed metadata totals approximately 4.7 KB (two source bounds and two destination
bounds per row, coarse masks, flat block shades and run ends); there are no new
frame allocations or bulk buffers. Cache-work scratch is never borrowed. Row
checkpoints and frozen frame state are retained. Mode switching continues to
clear FPS history, and the previous seventeen modes remain available. Modes 19
and 20 are checked against baseline final pixels in every chapter, independently
of mode 18; tests also assert that the other two approaches stay disabled.

Validation includes 180 synthetic transforms with an independent per-pixel
foreground oracle, full fills interrupted by single-pixel lines, culling versus
unculled low sampling, and 120 terrain states drawn over opposite backgrounds
to prove coarse opaque blocks. All ten chapters at three views retain the exact
untransformed foreground source; opaque scenes and no-motion output match the
old pipeline. Repeated output is deterministic. Before/after scene renders were
visually inspected; exposed backgrounds can differ slightly in filtering and
boundaries between the two sampling resolutions still need device motion review.

A host render-only benchmark runs all four choices independently (10 chapters,
three starts, 100 moving frames, two reversed-order repetitions: 6000 frames per
mode). Device packing, display scans, input servicing and PSRAM behavior are
excluded. Reproduce with the compile command in
`test/native_apps/hollow_trail_camera_bench.c`. Device FPS remains unmeasured;
compare each of modes 18–20 with mode 1 on matching routes using the full
ten-second window.

One host run of the separated modes measured:

| Mode | Render time for 6000 frames | Relative to baseline |
| --- | ---: | ---: |
| 1 Baseline | 3195.610 ms | 1.000× |
| 18 Low background camera | 3426.680 ms | 1.072× |
| 19 Coarse occlusion | 3580.536 ms | 1.120× |
| 20 Solid fill camera | 2889.096 ms | 0.904× |

Lower time is better. Only solid-fill projection improved this host workload;
these results do not predict the ESP32-S3 FPS ranking. Baseline remains default.
