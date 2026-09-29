# Hollow Trail neural scenery upscaler

Hollow Trail builds cached depth/fog/shadow layers at logical resolution. The learned path reduces the *per-frame* lighting composition grid to 120x68, reconstructs the normal 240x135 scenery buffer, and then leaves the existing fast 240x135 -> 480x270 interpolation and final 1bpp/dither path unchanged. Character geometry, traversal objects, prompts, HUD text, camera transform, vignette geometry, and panel packing remain deterministic raster code.

## Model

The deployed model is deliberately tiny: a 3x3 grayscale patch (9 inputs) feeds 5 ReLU hidden units, which predict three signed residuals for the interpolated pixels of a 2x2 destination block. The original source sample is an exact anchor and is never modified. A cheap central 2x2 range test runs first; patches below 80 gray levels use the exact bilinear reconstruction and skip the neural MACs. On the held-out set only 8.97% of source patches invoke the network.

The exported inference path uses only integer multiply/add/shift operations and 60 bytes of trained weights plus 32 bytes of integer biases. On the held-out gate distribution this topology performs about 44,000 neural multiply-accumulates per reconstructed frame, roughly one-seventh of the earlier 24-hidden candidate. It requires no TensorFlow Lite, ESP-NN, heap allocation, firmware API, or additional capability. The 120x68 composition surface owns 8,160 bytes within the app's existing single PSRAM allocation. It must not alias cache scratch: a lookahead job can remain unfinished across visible frames, and its source buffers must survive until publication.

## AI-trained physical dithering

The final neural 960x540 reconstruction was removed after hardware testing showed that it reduced frame rate. The fast **AI** renderer remains the baseline.

The new **AI + Dither** experiment targets the output stage instead. Its offline trainer distills the existing clean bilinear+Bayer 8x8 result into a 1 KiB deterministic halftone LUT. A 6-bit darkness bucket plus the logical pixel's 4x4 phase selects one learned 2x2 physical dot pattern. Runtime therefore uses one lookup and bit packing per 480x270 source pixel: no neural MACs, no error-diffusion state, no 960x540 grayscale buffer, and no extra full-frame pass.

The current held-out synthetic validation reproduces 94.8492% of complete 2x2 teacher patterns and 98.6319% of individual teacher dots. This is a performance experiment; the physical panel remains the authority on whether the learned pattern is visually as clean as the existing dither.

## Hardware A/B mode

The former DSP16, legacy-renderer, and neural-960 experiments are retired. Hollow Trail now uses the paused-game control for a focused output-stage comparison:

- **AI**: quarter-resolution AI lighting composition and trained reconstruction, followed by the existing clean physical dither.
- **AI + Dither**: the same AI renderer, but the physical 960x540 dot pattern is produced by the learned halftone LUT.

Pause the game and press **B or Up** to switch. The performance counters reset on every switch, and the pause screen identifies the active renderer, so FPS and RENDER time can be compared under the same scene. Because the legacy compositor is gone, its full-size focus maps are gone too; the two AI focus maps now use the 120x68 lattice, reclaiming roughly 95 KiB of PSRAM.

## What it accelerates

Before this change, each visible cached layer is blended into a 240x135 lighting surface every rendered frame. The new path samples those same cached near/wide blur layers onto 120x68 (about one quarter as many composition samples and cached-layer reads), then reconstructs 240x135 with the trained residual model. Cached near/wide strip construction itself is deliberately unchanged in this iteration; it occurs in the bounded look-ahead cache worker rather than the main frame compositor. Hollow Trail's existing `RENDER` and `CACHE` counters therefore distinguish the model's intended frame-time gain from cache-build cost on hardware.

## Training teacher

`scripts/train_hollow_trail_neural_upscale.py` deterministically generates a procedural teacher set shaped around Hollow Trail's renderer: layered grayscale silhouettes, lines, fog gradients, offset soft shadows, and sharp occlusion edges. The high-resolution teacher is 96x64. Even-coordinate decimation becomes the 48x32 source, matching the runtime compositor lattice. The model learns the residual between bilinear expansion and the original teacher.

Training uses seed `20260928`, generates 110,000 candidate training patches, trains on the 10,368 patches that cross the deployed edge gate, and evaluates on 28,000 held-out validation patches. The loss is Smooth L1 with extra weight on high-local-range patches. The final float model is quantized to the exact integer format used in `Apps/hollow_trail_neural_upscale.inc`.

Held-out validation for the committed model:

| Upscaler | MAE (gray levels) | PSNR |
| --- | ---: | ---: |
| Bilinear teacher reconstruction | 3.6350 | 24.7789 dB |
| Quantized neural residual, central-2x2 gated | 3.3985 | 24.9796 dB |

These figures measure the synthetic teacher set, not ESP32-S3 frame time and not a claim about perceptual preference on the panel. Hollow Trail's existing `RENDER` profiling remains the authoritative runtime measurement.

## Reproduction

The trainer requires Python, NumPy, OpenCV (`opencv-python-headless` is sufficient), and PyTorch. Run it from the repository root. By default it trains for 60 epochs; `--export Apps/hollow_trail_neural_upscale.inc` regenerates the deployable constants and `--report <path>` emits the metrics/weights JSON. The stdlib-only host contract test `python test/hollow_trail_neural_upscale_test.py` checks the committed integer weights against fixed golden vectors.
