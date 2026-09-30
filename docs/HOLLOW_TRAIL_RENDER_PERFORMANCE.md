# Hollow Trail grotto rendering cost

Hollow Trail 1.1.10 follows the neural renderer in 1.1.9. The device report was 130% of legacy FPS for neural rendering (30% more FPS, approximately 23% less elapsed time per frame). The more specific water-scene report was 4.8 FPS legacy versus 5.5 FPS AI: approximately 15% more FPS, or 208 ms versus 182 ms per frame. These reports do not establish the performance of the additional grotto changes below.

## Why the grotto bypassed much of the neural benefit

The 120x68 neural path accelerates the cached scenery compositor. The grotto then renders an independent 480x270 cave-lighting/water field. Through camera X=700 that field is completely opaque, replacing every background pixel. Previously the engine composed/reconstructed/upscaled the hidden background, drew hidden geography/detail/water, then replaced it all. Inside the grotto pass it also computed glow and two cave-wall blends for pixels subsequently replaced by opaque rock or water.

The new path:

- skips hidden background raster work only while the grotto is fully opaque;
- preserves the original background and crossfade for camera X=701 through X=899 and restores the normal scene after the fade;
- rejects opaque rock before lighting, and shades water without evaluating discarded cave lighting;
- evaluates row constants once per row and reuses edge weights;
- writes opaque pixels directly instead of reading/blending discarded destination pixels.

No resolution reduction, new learned approximation, allocation, or removal of visible effects is involved. Background lookahead remains available so the exit can prepare its visible strips. The camera transform, vignette, boat, character, water/reflections, collision, input, and neural/legacy toggle are retained.

## Pixel and build checks

`test/native_apps/hollow_trail_grotto_test.c` compares 300 views against the previous grotto raster, covering camera positions, vertical offsets, vista scales, and both ends of the boat's channel. It also checks ten original complete-frame hashes, including the crossfade boundary, with camera transform and vignette. Opaque grotto output matches in AI and legacy modes. It checks cooperative checkpoints and allocation bounds.

The targeted grotto, cache, render/input-service, submission pipeline, and full gameplay tests passed with AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer was disabled because this host cannot inspect `/proc` tasks. The ESP32-S3 app builder and native ELF structural validator passed. Source, emitted sidecar, and catalog use 1.1.10.

## Host comparison, not device FPS

Same host, `cc -std=c11 -Os`, 400 warmed renders of each frozen scene, CPU time per frame. Packing measured separately. The baseline is master `abca9f8` (Hollow Trail 1.1.9); both versions use the AI compositor. All ten full-frame hashes match.

| Camera X | Vista | Before render ms | After render ms | Ratio |
| --- | --- | ---: | ---: | ---: |
| 0 | 0 | 1.339 | 0.728 | 1.84x |
| 120 | 256 | 1.473 | 0.795 | 1.85x |
| 240 | 0 | 1.409 | 0.761 | 1.85x |
| 420 | 256 | 1.545 | 0.877 | 1.76x |
| 680 | 0 | 1.572 | 0.834 | 1.88x |
| 700 | 256 | 1.642 | 0.888 | 1.85x |
| 701 | 0 | 1.559 | 1.066 | 1.46x |
| 800 | 256 | 1.631 | 1.172 | 1.39x |
| 899 | 0 | 1.677 | 1.125 | 1.49x |
| 901 | 256 | 0.638 | 0.677 | 0.94x |

The last row is outside the optimized scene and showed a small slowdown in this run; these host samples are not a guarantee of gains on the ESP32-S3. They exclude display scans, input/scheduler servicing, and cache misses. They must not be multiplied by the user's neural/legacy device ratio to predict FPS.

Reproduce with `test/native_apps/hollow_trail_render_bench.c`, compiling it with `-Os -IApps -Ilib/NativeApps/include`. To compare a baseline checkout, place that checkout's `Apps` include directory first while using the same benchmark source. The optional first argument is the number of iterations. Output reports camera, vista, render/pack CPU milliseconds, and the frame hash.

## On-device diagnosis

The pause panel retains FPS, scan rate, RENDER/PACK/WAIT, CACHE/COPY/INPUT, and scan preparation/DMA/pacing. It now adds:

- **BG**: visible cache work, scenery composition/reconstruction/upscale, and background lightning;
- **WORLD**: grotto, terrain, traversal, puzzles, character, and weather;
- **CAMERA/EDGE**: affine camera transform and vignette.

These are averages over submitted gameplay frames, frozen for inspection when paused. Like RENDER, stage durations include cooperative input/yield time; INPUT is overlapping time, not an extra cost to add. HUD drawing remains in overall RENDER. The clock is the existing app API and is sampled only at stage boundaries.

At 8.3 FPS a frame averages about 120 ms; at 24 FPS the budget is about 42 ms. Device timings determine the next step. Low-resolution learned reconstruction is a candidate for expensive changing fog/shadow/reflection fields, provided its inference plus reconstruction costs less than the replaced pass. Static terrain should be cached or culled rather than inferred every frame. Panel scan/DMA and memory transfer costs need driver or raster-pipeline improvements; another scenery model does not remove those costs.

## Forest revision 1.1.25

Level 1 restores composed depth and light while retaining the original living
foreground trees. It replaces the opening rock-operated lift with a falling
snag and gives the loose stone a physical hollow. The composed background is
opaque: obsolete strip composition, startup warming and lookahead are skipped
for this chapter. The first unused depth-2 near/wide cache pair holds a complete
background keyed by horizontal camera, altitude and vista scale. No additional
framebuffer is allocated. The foreground remains dynamic and full resolution.

The raster shades the nearest visible ridge only, samples soft distant tones
at 2x scenery resolution, and restricts shaft lighting to its two narrow spans.
The foreground trunk's bent centreline and taper also define its walkable top.

Host checks cover deliberate interaction, no absent-log ledge capture, no
mid-fall footholds, settled contact, death/restart behavior, an input-driven
stone move into the hollow, standing on that stone, and fresh/cached background
agreement across camera positions, altitudes, vistas and chapter changes.
The complete input-only ten-chapter route uses the actual fallen log and rope
in level 1. Original tree intent/contact tests remain. The three forest golden
frames were reviewed and updated; the other 27 remain unchanged. The render
service test now checks coherent snapshots across enough frames to observe
both input press and release, rather than requiring slow cold-strip work in
one frame.

`test/native_apps/hollow_trail_forest_bench.c` compares 800 settled and 800
moving views with the same `-Os` build and source against master `29222445`
(1.1.24). Samples on this shared host ranged from 0.26–0.49 ms settled and
0.43–0.55 ms moving for this revision; master sampled 0.35–0.37 ms settled and
0.38–0.39 ms moving. The restored depth still adds work to changing views;
these CPU samples do not establish device FPS or a guaranteed speedup.
They exclude output packing, display scans and scheduler/input time.

The ESP32-S3 builder and ELF structural validator pass. Manifest, embedded
version, generated sidecar and catalog agree on 1.1.25, with unchanged minimum
firmware 1.3.37. Device performance and visual acceptance remain unmeasured.
