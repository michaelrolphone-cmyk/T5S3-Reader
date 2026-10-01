# Hollow Trail native-resolution A/B mode

Hollow Trail 1.1.34 changes the high-detail A/B mode to a power-of-two-friendly split:

- **backgrounds:** restore the original full native **960×540** path
- **foreground / gameplay geometry:** **960×270** detail raster
- **panel output and camera:** remain **960×540**

Pause the game and press **B / Up** to cycle to **DETAIL 960X270**. The production baseline remains the default.

## Why 960×270

Baseline gameplay geometry is 480×270. The earlier full-native experiment was 960×540, which requires roughly four times as many raster samples as baseline for foreground shapes. The new foreground mode doubles only the horizontal axis, so it uses about twice the baseline raster sample count while preserving native X resolution.

The 270 computed foreground rows are written directly into the physical 960-pixel-wide surface and each computed scanline is duplicated to its paired 540-row output line. This is a simple 2:1 operation: no 3/2 scaling, fractional coordinate grid, extra framebuffer resampler, or per-pixel divide is introduced.

## What remains full native

Forest, city, authored-chapter backdrops, the dedicated boat grotto, camera transform, vignette and final monochrome packing retain the original 960×540 implementation from the first native-resolution mode. The foreground reduction begins after those background/grotto passes.

The reduced foreground includes traversal terrain and props, character geometry/animation, trees, ropes, evidence/puzzle geometry, weather, and near foreground occluders. Horizontal edges may still occupy odd physical columns; vertical detail is intentionally quantized to the baseline 270-row grid.

## Purpose

This is an A/B quality/performance compromise. It aims to keep the sharper silhouette and horizontal contour detail of the native mode while avoiding the full 4× foreground raster cost. Device FPS/RENDER/PACK measurements remain the authority for whether the tradeoff is worthwhile.
