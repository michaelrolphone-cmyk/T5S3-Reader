# Preserve XTH tones on monochrome surfaces

Baseline: X4 working head 36891e71ab651152c618d60d1d248abb4a7f44ff.
Branch: fix/xth-mono-tone-fallback. Firmware 1.3.128 -> 1.3.135, above the
separate default-entry 1.3.133 and bookmark-performance 1.3.134 reservations.
No independently distributed app, driver or provider payload changes.

## Reproduced reachable gap

The shared Reader routes XTC/XTCH books into XtcReaderActivity. Its 2-bit XTH
renderPage first paints all nonwhite source tones black as the base for later
grayscale planes. X4's MONO1 ProviderDisplaySurface deliberately returns false
from captureGrayscaleBaseBuffer. The old failure path immediately displays that
intermediate frame, so both dark and light gray become solid black.

Executing the complete original production renderPage with four encoded tone
patches yields 0/32/32/32 black pixels per 32-pixel white/dark/light/black patch.
This is an actual software fallback defect, not evidence of a panel waveform
failure. The successful T5 grayscale path remains separate and unchanged.

## Current X4-base refresh (2026-10-06)

The existing PR420 branch includes X4 source b91bc323 while retaining original
repair head 9b2fa348 as its first parent. Only the firmware reservation conflicts:
1.3.135 / upstream 1.3.143 advances to 1.3.151. The complete original render fix
and pixel regression are unchanged, and all upstream source changes remain.
Normal and ASan/UBSan production pixel regressions pass on the combined tree.
Exact-head target CI is recorded on the PR; hardware is unavailable and unrun,
not a prerequisite for software integration. No app/driver payload change,
PR350/master write, release or device action is included.

## Repair

When capture is unavailable or fails, clear the intermediate frame and draw
source tones with the existing GfxRenderer monochrome dither primitives. White
and black retain their exact output; dark/light gray use existing 50%/25% black
patterns. These are existing renderer densities, not calibrated equivalents
of panel gray luminance. This is monochrome spatial dithering, not hardware
grayscale.

The pass is clipped to the visible logical raster, uses the existing orientation
transform, requires no new image/plane allocation or storage operation, and
presents once. Fixed-row and elapsed-time checkpoints yield to the scheduler,
including rollover. The original page allocation is freed before presentation;
normal page refresh cadence is retained. Existing one-bit pages and successful
grayscale rendering are untouched.

## Verification

The test compiles complete production renderPage and the real GfxRenderer
rotation, pixel packing and dither methods. It compares every physical raster
pixel against an independent inverse-coordinate/tone oracle on small, X4
800x480 and T5 960x540 panels, all four orientations, and clipped oversized
source pages. Exact uniform-tone densities, repeatability, one-bit output,
allocation failure, failed load, retry, balanced cleanup, refresh cadence and
row/elapsed/rollover yields are asserted. Successful T5-style grayscale output
is decoded using the actual HalDisplay grayscale-value function and compared
against all four source tones.

The test uses supported column-plane fixtures with byte-aligned heights.
Inherited page-size/format validation and decoder assumptions are unchanged;
this repair does not claim malformed-file hardening.

The original source fails the same exact-pixel regression; repaired source
passes strict normal and ASan/UBSan builds. Page-loading and display transport
are fixtures; these checks do not claim file-parser, physical panel or latency
qualification. Existing production XTC parser tests are run separately. Local
LeakSanitizer is disabled under ptrace. Full aggregate and exact-head hosted
results are recorded on the PR; PlatformIO is unavailable locally.

No PR/source-branch integration, release, flash or device operation is included.
