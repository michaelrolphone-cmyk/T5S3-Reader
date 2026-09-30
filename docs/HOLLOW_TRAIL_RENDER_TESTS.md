# Hollow Trail production renderer — 1.1.20

The renderer now permanently uses the device-tested mode 24 combination plus
mode 13: SIMD upscale, reconstruction, compositing and vertical blur; packed
vignette; compositor tile planning; and horizontal blur interior filtering.
AI scene reconstruction and the successful SIMD output packer remain enabled.

The mode selector and unused experiment code are removed. Pause retains level
selection, journal access, resume/exit, stage timings and rolling ten-second FPS.
B/Up no longer switches rendering and cannot queue a jump while paused. Resume
and journal transitions continue to reset the FPS window.

Removed: low-background camera, coarse occlusion, solid-fill camera projection,
packed/flat camera alternatives, triangle stepping, bounded focus experiment,
neural patch cache, learned output dithering, legacy non-AI scene composition,
and the optional internal-SRAM experiment workspace. Two focus maps are now sized
for the retained 120×68 reconstruction grid rather than the legacy 240×135 grid.
The standard full-resolution camera and the existing border are unchanged.

Retained safety: runtime-aligned SIMD constants, four independent kernel startup
checks, original output-packer self-test, per-stage scalar fallbacks, bounded
render checkpoints and cleanup. These fallbacks are production paths, not
selectable experiments. No firmware update is required; app 1.1.19 → 1.1.20.

Validation uses frozen frame and packed-output hashes captured from 1.1.19 mode
24 + 13 before cleanup, across all ten chapters and three views. Every SIMD
readiness mask must reproduce those hashes. An independent clamped-sample oracle
checks horizontal blur boundaries, narrow regions and random input. Existing
SIMD arithmetic, cache, grotto, gameplay, input and cooperative-service tests
remain. FPS tests cover rolling updates, resets and timer wraparound.

Build: `python3 scripts/build_all_apps.py --id hollow_trail`.
Native tests: `bash test/run_native_app_test.sh`.
Host checks cannot establish ESP32-S3 FPS; the chosen combination comes from
owner hardware measurements.
