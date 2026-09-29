# Hollow Trail independent render tests — 1.1.16

The owner measured only about +0.2 FPS for the combined expansion. This build
isolates its operations to expose individual gains or regressions, and adds eight
shared-renderer experiments. All modes keep AI composition and the proven SIMD
packer when its device self-test passes. No learned dither, neural 960 mode,
scene-specific shortcut, effect removal or model-weight change is introduced.

Pause and press **B/Up** to cycle. The pause screen shows the numbered experiment.
Each test changes only the named operation relative to the packing baseline,
except the explicitly named all-four comparison:

| Test | Selected change |
| --- | --- |
| 1/14 Packing baseline | AI with the existing SIMD packer; all extra operations disabled. |
| 2/14 SIMD upscale | Vector 240×135 → 480×270 interpolation only. |
| 3/14 SIMD reconstruction | Vector 120×68 reconstruction interpolation and edge gate only; residual inference unchanged. |
| 4/14 SIMD compositing | Vector focus/depth blending only. |
| 5/14 SIMD blur / cache | Fused vector vertical blur output and running sums only. |
| 6/14 Packed camera | Two horizontal interpolation rows in independent 16-bit lanes of one 32-bit word. Original vertical interpolation/rounding retained. |
| 7/14 Packed vignette | Symmetric fade pixels multiplied in two 16-bit lanes, with exact division by 255 using shifts/adds. |
| 8/14 Triangle stepping | Exact integer quotient/remainder edge stepping replaces two divisions per scanline in shared triangle rasterization. |
| 9/14 All four SIMD | Previous expanded behavior: tests 2–5 together. The additional CPU experiments stay off. |
| 10/14 Bounded focus math | Clamp distance first; compute the remaining /200 through an exact bounded 32-bit reciprocal. |
| 11/14 Flat camera skip | Skip interpolation only when all four source samples are identical. |
| 12/14 Neural patch cache | Reuse an existing model result only for an identical nine-byte input patch. |
| 13/14 Blur interior | Peel clamped edges from the horizontal running-sum loop; directly address interior samples. |
| 14/14 Compositor tile plan | Calculate horizontal tile spans once per composition and reuse them for all rows. |

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
All experiments operate in common renderer functions. They preserve the
existing raster size, filters, model, geometry, camera trajectory and checkpoints.

## Validation

- ASan/UBSan: mode isolation/readiness combinations, all 256 camera fractional
  combinations over 20,000 random patches, every reachable vignette quotient,
  3,000 triangles including clipping/flat edges/projected vistas, and 30 fresh
  scene views across all ten chapters in every mode match baseline pixels.
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
complete fresh renders in all fourteen modes across every chapter. Rolling FPS
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
