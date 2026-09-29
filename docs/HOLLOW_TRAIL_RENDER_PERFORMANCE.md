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
