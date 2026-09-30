# Boot wisp clearing

T5S3 cold boot retains the physical panel through M5GFX initialization and
replaces the raw video's white/black/white reset with a spatial endpoint scrub.
Recovery, panic, SD-error initialization and the non-video EPD47 fallback retain
the ordinary clear. Desk-clock wake routing is unchanged. The existing logo
animation follows the scrub.

`NativeVideoBootScrub.h` defines curling arrival contours across all 960×540
pixels. A pixel receives no drive until its arrival, then three black scans and
six white scans, then no drive. All pixels finish within 24 scans (about one
second at the existing 24 Hz target). This treats the retained image as unknown;
it does not draw a white framebuffer over the whole screen before the wisps.

The 506 KiB arrival map uses PSRAM, with 4-pixel grid evaluation and interpolated
full-resolution contours. Preparation has a 1.5s deadline and item/time yields.
Scanning has a 1.8s deadline and uses the existing DMA timeout and pacing. The
map is freed after the final drive has drained, before logo/provider loading.
Allocation/scan failure returns to the existing safe renderer fallback, which
may perform a conventional clear. An uncertain hardware teardown retains
ownership for retry, never starting a second panel owner.

After scrub, the mono engine starts from settled white. After the logo fade,
boot explicitly submits white and waits for drive completion. M5GFX resumes
without its redundant initialization clear; a confirmed white handoff suppresses
the forced initial full refresh. Normal app exits still request their full
refresh, and grayscale destination content retains its normal gray waveform.

Firmware version: 1.3.43 → 1.3.44. No independently distributed package changes.

## Preview and validation

Run `python3 scripts/preview_boot_scrub.py /tmp/boot-wisp.gif` (C++ compiler and
Pillow). The generator uses the production drive code, rotates it to portrait,
and includes 600ms before / 800ms after holds around the one-second scrub.
It models commanded endpoints, not real pigment response or ghosting.

`python3 test/boot_animation_contract_test.py` exercises every pixel's packed
drive commands, retain/black/white coverage, final white, no global black flash,
plus the existing startup cancellation, ownership retry and fallback tests.

Device qualification remains necessary: actual elapsed time is logged as
`boot wisp scrub complete`, and retained dark/gray screens should be checked
for residual ghosting, especially at different panel temperatures. This is a
bounded spatial version of the existing endpoint pulses, not a claim that a
one-second scrub matches the panel's full temperature-compensated waveform.
