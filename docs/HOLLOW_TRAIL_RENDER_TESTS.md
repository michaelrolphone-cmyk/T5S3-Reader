# Hollow Trail independent render tests — 1.1.19

The owner reports mode 24 as the fastest tested combination. It is now the
launch default and the always-enabled base for every experiment: SIMD upscale,
reconstruction, compositing and blur, plus packed vignette and compositor tile
planning. The proven SIMD packer and AI scene renderer remain enabled.

Every selection uses **mode 24 baseline OR selected experiment**, without running
any operation twice. Selecting a new experiment replaces the previous additions;
it never removes baseline optimizations. Pause → B/Up cycles the existing mode
numbers and resets the ten-second FPS window. Failed SIMD stages fall back
individually; other baseline stages remain active.

| Selection | Effective rendering |
| --- | --- |
| 1–5, 7, 9, 14, 21–24 | Mode 24 baseline; these selections add no new bits. |
| 6 | Baseline + packed camera |
| 8 | Baseline + triangle stepping |
| 10 | Baseline + bounded focus math |
| 11 | Baseline + flat camera skip |
| 12 | Baseline + neural patch cache |
| 13 | Baseline + blur interior |
| 15 | Baseline + internal neural workspace |
| 16 | Baseline + internal blur workspace |
| 17 | Baseline + internal packing workspace |
| 18 | Baseline + low-resolution background camera |
| 19 | Baseline + coarse background occlusion |
| 20 | Baseline + solid-fill camera projection |
| 25 | All optimizations combined |

Redundant selections are labeled **MODE 24 BASELINE**, and additions are labeled
**BASE + ...**. The game starts at selection 24. All-combined remains selection
25. Mode 18 and 25 inherit the low-background camera filtering change; other
selections retain baseline pixels.

All-combined composes flat rejection with packed interpolation, projects verified
fills inside foreground spans, and combines occlusion with planned SIMD blending.
Neural cache misses use internal weights when available. Blur, neural and packing
reuse one existing 4 KB workspace sequentially, reinitializing their data.
Unavailable internal memory falls back for those stages only.

App **1.1.18 → 1.1.19**; no firmware change is required. Earlier independent-mode notes below describe the original
experiments; all selections now include the mode 24 baseline.

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
