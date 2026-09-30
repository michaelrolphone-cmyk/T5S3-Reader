# Shared video and static-driver wisp clearing

T5S3 cold boot retains the physical panel through M5GFX initialization and
replaces the raw video's white/black/white reset with a spatial endpoint scrub.
Every fresh shared video `start()` or `start_format()` now uses the same scrub,
including Springboard, Hollow Trail, Risc Strike and 3D Viewer, in monochrome
and four-gray modes. A repeated start on an already-running same-format service
returns its surface without replaying the effect; a real stop/start replays it.
The video API layout is unchanged and app rebuilds are unnecessary.
GameBoy's independently compiled `epd_video` backend is outside this shared path.

T5S3 M5GFX quality/text refreshes, including initialization and recovery clears,
now use the same spatial cleaning. The other-board EPD47 backend is unchanged. Desk-clock wake routing is unchanged. The existing logo
animation follows the scrub.

`NativeVideoBootScrub.h` defines curling arrival contours across all 960×540
pixels. A pixel receives no drive until its arrival, then three black scans and
six white scans, then no drive. All pixels finish within 16 scans (about two-thirds of a
second at the existing 24 Hz target). This treats the retained image as unknown;
it does not draw a white framebuffer over the whole screen before the wisps.

The 506 KiB arrival map uses PSRAM, with 4-pixel grid evaluation and interpolated
full-resolution contours. Preparation has a 1.5s deadline and item/time yields.
Scanning has a 1.8s deadline and uses the existing DMA timeout and pacing. The
map is freed after the final drive has drained, before returning the surface to the caller. The speedup compresses pixel arrival
times from 0–15 to 0–7 scans; black/white pulse counts and scan cadence stay unchanged.
Allocation/scan failure returns false after safe teardown. Boot then uses its
existing safe renderer fallback, which may perform a conventional clear; app
callers use their existing startup-failure paths. An uncertain hardware teardown retains
ownership for retry, never starting a second panel owner.

After scrub, both engines start from settled white: mono state 0xfc, gray state
0x00, and zeroed target buffers. The first gray target therefore ramps directly
from white without replaying the gray unknown-state reset. After the logo fade,
boot explicitly submits white and waits for drive completion. M5GFX resumes
without its redundant initialization clear; a confirmed white handoff suppresses
the forced initial full refresh. Normal app exits still request full refresh, which now uses the static scrub.

The pinned M5GFX patch runs `M5WispRefresh.h` inside its existing worker for
quality/text updates. It uses existing DMA buffers and bus ownership, retains
pixels outside the update rectangle, freezes target pixels in a PSRAM snapshot,
performs the scrub, then draws RiscRTE's four gray levels with 0–3 black pulses
from white. Other 4-bit input shades round to the nearest supported gray. It
commits settled driver history only after the last DMA has completed; queued
updates therefore compare against the image actually drawn. Fast/fastest writes
keep their existing path. This replaces the original eraser *and* the flashing
quality/text tone sequence, rather than running those again after the scrub.

Static working memory is bounded to 759 KiB (arrival map plus packed target
snapshot), freed after each update. Preparation has a 1.5s deadline and rendering
a 2s scan-loop deadline, with scheduler pauses every scan and timed preparation
checkpoints. Bus DMA waits retain the existing M5GFX API behavior. Cancellation
leaves history uncommitted and drains the current frame before returning. An
allocation, geometry, power or deadline failure uses the original waveform
fallback. The scrub is ~0.67s; three tone scans add ~0.13s, plus preparation/power
time. This is a target, not a measured hardware latency.

Partial quality/text updates use a center-out elliptical ripple fitted to the
rectangle. A single row or two-pixel column becomes a one-dimensional outward
wave. Spatial rounding feathers the moving front over one scan. A narrow rim
(up to 12 pixels) scatters arrival across the existing seven-scan window to
replace the hard cutoff with a stippled shimmer. Every
pixel keeps the original contiguous three black/six white cleaning pulses at
42ms cadence. Target tones are restored immediately behind each pixel's wave,
rather than waiting for a uniform white rectangle before redrawing. The total
19 scans and drive intensity are unchanged. Outside pixels remain undriven;
a strict rectangular update cannot make its boundary physically invisible on
arbitrary surrounding content, but it no longer cuts a repeating wisp field.
Full-screen clearing retains its existing pattern and target timing.

Firmware version: 1.3.46 → 1.3.47. No independently distributed package changes.

## Preview and validation

Run `python3 scripts/preview_boot_scrub.py /tmp/boot-wisp.gif` (C++ compiler and
Pillow). The generator uses the production drive code, rotates it to portrait,
and includes 600ms before / 800ms after holds around the two-thirds-second scrub.
It models commanded endpoints, not real pigment response or ghosting.

`python3 test/boot_animation_contract_test.py` exercises every pixel's packed
drive commands, retain/black/white coverage, final white, no global black flash,
plus the existing startup cancellation, ownership retry and fallback tests.
`test/hal/video_start_scrub_test.py` compiles the production shared start function
against hardware doubles and checks both pixel formats, settled-white state,
the first gray target, idempotent starts, allocation/power/deadline failures,
and refusal to start over retained hardware after failed teardown.

Device qualification remains necessary: actual elapsed time is logged as
`video wisp scrub complete`, and retained dark/gray screens should be checked
for residual ghosting, especially at different panel temperatures. This is a
bounded spatial version of the existing endpoint pulses, not a claim that a
one-second scrub matches the panel's full temperature-compensated waveform.

`test/hal/static_wisp_refresh_test.py` checks the pinned patch, full and partial
update drive coverage, equal cleaning doses, all four output tones, target
snapshot isolation, state commit, padding, cancellation and allocation fallback.
The original one-second boot effect was confirmed visually by the owner. The
faster overlap and static-driver gray reproduction still need device observation.

`python3 scripts/preview_partial_ripple.py /tmp/partial-ripple.gif` renders the
production partial commands on a retained page, with a large and narrow update.
Its pulse-to-gray accumulation is illustrative, not measured pigment behavior.
