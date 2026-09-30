# Hollow Trail camera comparisons

Hollow Trail 1.1.24 adds a pause-screen graphics selector. Pause with Select/Down,
then press B/Up to cycle modes. Resume to collect a fresh 10-second FPS window;
render, pack, wait, background, world and camera/edge timings remain available.
The selection lasts for the app session; startup always selects baseline.

| Mode | Rotation | Zoom | Final camera sampling |
| --- | --- | --- | --- |
| Baseline | Original | Original | Unchanged nearest-neighbor affine loop |
| Rotation off | Disabled | Breathing/drop and vista retained | Axis-aligned row sampling |
| Zoom off | Original | Breathing/drop, safety crop and vista scaling disabled | Rotation with edge-clamped source samples |
| Both off | Disabled | Disabled | No final camera sampling pass |

Baseline still uses the exact original coefficients and raster path. Comparisons
retain simulation state, camera translation, scenery, AI composition, dithering
and display settings. Vista's vertical camera tracking also remains unchanged;
only its draw-time size scaling is disabled by the no-zoom modes.

Originally zoom and rotation shared `ht_affine_into`: each output pixel advanced
both source coordinates, rounded both and calculated a source-row offset. Zoom
without rotation needs only one source row per output row. `ht_zoom_into` selects
that row once and advances only the horizontal sample coordinate. Zoom is still
resampling and memory traffic; it is not free, but it does not require the general
affine loop. The separate vista effect scales geometry coordinates before drawing
and does not rotate/resample the finished raster.

Pure rotation exposes corners previously hidden by the safety crop. Zoom-off
clamps those samples to the source edge, which can stretch edge pixels and adds
bounds work; account for that when interpreting its performance. Both-off renders
directly into the output buffer, bypassing transform staging and sampling. This
still retains the normal low-resolution scenery reconstruction/output scaling.

Validation: unchanged baseline golden frames, zoom sampling equivalence to the
zero-turn affine reference, independent pure-rotation coordinate checks, both-off
animation/vista invariance, and selector cycling/held-button behavior. Host checks
and an Xtensa app build do not establish device FPS gains.
