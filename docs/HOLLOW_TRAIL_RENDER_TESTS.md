# Hollow Trail frame strategy tests — 1.1.21

The baseline remains the device-tested mode 24 + 13 production renderer.
Pause → **B/Up** cycles five tests. Each selection starts from the same baseline;
only the ALL THREE mode combines the additions. Mode changes discard pending
frames, invalidate retained scenery and reset ten-second FPS/stage statistics.
Pause/journal resume also starts with a fresh background and motion history.

| Mode | Change |
| --- | --- |
| 1 BASELINE | Existing AI/SIMD renderer, full scenery and bilinear camera every frame. |
| 2 ALTERNATE BACKGROUND | Alternate fresh and retained scenery frames; all interactive terrain, evidence, player, moving objects and weather still draw each frame. |
| 3 NEAREST CAMERA | Use rounded nearest source pixels for camera rotation/zoom instead of four-tap interpolation. |
| 4 MOTION LOW + CHARACTER DETAIL | During player/camera movement, sample the final scene at 2×2 logical-pixel blocks outside a 144×112 full-detail source region centered on the character. Restore full sampling when motion stops. |
| 5 ALL THREE | Background alternation plus nearest sampling plus motion-dependent peripheral resolution. |

The motion test reduces the final camera sampling grid from 480×270 to 240×135
in the periphery, then fills each 2×2 block. It does **not** lower geometry
construction resolution. The protected region follows the character through
vista scaling and the affine camera; UI is drawn afterward at full resolution.
Nearest sampling in mode 5 also applies to full-detail character-region samples,
but those pixels are still evaluated individually. This is deliberately a
visual-quality/performance tradeoff, not a claim of pixel-equivalent output.

Background retention includes layered scenery reconstruction/upscale, geography,
landscape detail, water/grotto art, background silhouettes and story landmarks.
It excludes evidence, terrain/interactive solids, puzzles, character, weather and
UI. Retained scenery is held at its previous screen position for one frame;
parallax/scenery animations can visibly step or lag. There is no costly image
reprojection. Lookahead cache work is skipped on retained-background frames.

The optional 129,600-byte PSRAM raster is allocated once after display startup
and freed on exit. Allocation failure uses fresh backgrounds with a visible
pause-screen diagnostic; the other tests still work. Level changes, respawns,
large discontinuities, mode changes and grotto-opacity transitions invalidate
retention. Frame parity advances on completed render calls, not simulation ticks;
failed display submission retries the same prepared frame.

Production SIMD self-tests and scalar fallbacks remain intact. No firmware update
is required; app **1.1.20 → 1.1.21**. The default remains BASELINE.

Validation:
- Existing baseline scene/packing hashes across ten chapters and all SIMD
  readiness masks remain unchanged after separating scenery from interactive drawing.
- New tests cover fresh/reused alternation, an independent cheap-frame oracle,
  current actors, level/death/teleport invalidation, missing-memory fallback,
  stationary restoration, combined-mode composition, nearest pixel coordinates,
  border guards and full-detail character-region preservation.
- Native controls, gameplay, cache, journal, pipeline and cooperative-service
  checks remain in `bash test/run_native_app_test.sh`.
- Build with `python3 scripts/build_all_apps.py --id hollow_trail`.

These are experiments for device comparison; host correctness checks do not
establish device FPS or whether a visual tradeoff is acceptable.
