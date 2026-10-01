# Model Viewer pan/zoom preview

Model Viewer **1.2.3 -> 1.2.4**. Minimum firmware remains **1.3.29**, inherited
from the existing math API integration. No firmware, driver, display API,
catalog or release workflow change is part of this update.

## Preserved controls

The complete `model_viewer_controls.h` is unchanged. Rotation remains 3.90 rad/s,
RB+D-pad pan remains 2880 logical pixels/s, and LB+Up/Down zoom remains an 18.0/s
logarithmic rate. A retains its quarter-speed modifier. Existing start-ramp,
pending-time bound, release handling, bumper admission and mapped-input
arbitration are preserved. The app's input handler, render-service input
checkpoint and touch handler are also unchanged.

## Redraw path

The previous renderer transformed and rasterized the mesh again on every pan
and zoom frame. That work depends on mesh size, overdraw and zoom, even though
orthographic pan/zoom changes neither face lighting nor visibility ordering.

On the first controller pan/zoom at an orientation/material, the app builds a
640 x 640 atlas covering the entire normalized model, including geometry outside
the visible screen. Subsequent controller pan/zoom frames resample that atlas
instead of repeating triangle transforms, lighting, depth clearing and depth
tests. The same scalar/optional math-service path builds the atlas. Shaded
geometry retains every triangle and depth ordering; wireframe retains its
existing interactive triangle budget. Rotation and idle refinement continue to
use the normal mesh renderer. After movement stops, the existing idle pass
restores sharp mesh detail at the current view.

The atlas holds undithered ink rather than a screenshot. Fixed-point bilinear
sampling retains fractional pan/zoom positions. Two prefiltered mip levels
preserve thin wire coverage when shrinking, with adjacent-level blending to
avoid a level-transition pop. Dither is applied at destination screen coordinates
so its pattern is not magnified or moved with the model. Two bounded source-row
caches avoid repeatedly reading the same PSRAM rows during enlargement.

## Bounds and lifetime

Atlas and mip storage total **537,600 bytes**, allocated in PSRAM only and freed
on exit. Shaded atlas construction reuses the existing depth scratch; its
409,600 entries fit the current 425,068-entry allocation. A failed atlas
allocation falls back to the original renderer and is not retried every frame.
Rotation/material changes invalidate the key; incomplete/cancelled builds never
become valid. Cancelled draws are not submitted. Output is cleared before
resampling, so uncovered regions do not retain an old silhouette.

Loops are bounded by atlas dimensions, viewport dimensions and the existing
model triangle limit. Clear/mip/resample loops checkpoint every eight rows;
mesh work retains its bounded batches and elapsed-time service checkpoints.
Presentation continues through the existing video API. No input history,
background app task, movement tail or new hardware owner is introduced.

## Limits and verification

This is a moving preview: high zoom can show softer detail until the idle mesh
redraw. It reduces repeated mesh work and spatial aliasing; it does not raise the
physical display scan rate or guarantee on-device frame timing. The first cache
build after an orientation/material change still processes the mesh.

`test/native_apps/model_viewer_preview_test.py` compiles the production preview
header, shader, mesh/render helpers and current selector, substituting only
platform services. It tests both scalar/accelerated and wire/shaded paths;
40 changed consecutive pan/zoom frames with no mesh/depth pass after cache build;
key invalidation; offscreen model recovery; exact idle redraw; fractional
filtering, mip coverage/continuity and stationary dither; occlusion; allocation
failure, cancellation, publication backpressure, bounds and cleanup. It runs
with AddressSanitizer and UndefinedBehaviorSanitizer in the native-app runner.

Locally the new suite passed with and without sanitizers. An additional
before/after comparison against base `6d9cc316` produced identical exact-render
framebuffer bytes in 64 combinations of pose, wire/shading, interactive/refined
and scalar/accelerated rendering. The input/touch/service helper bodies were
compared unchanged. Local tests used fetched source and a selector-only controls
excerpt, not the complete firmware SDK/toolchain; no partial source or shim is
committed. Full repository/target builds and physical device testing are separate
checks and were not performed locally.
