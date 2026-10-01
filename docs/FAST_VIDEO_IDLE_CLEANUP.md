# Fast EPD idle cleanup — firmware 1.3.31

The shared native fast-video backend supplements normal transitions with a
finite idle reinforcement pass. Apps using this backend receive it automatically;
there is no app API or package update. It applies to the current in-firmware
backend in `src/native/NativeVideoBridge.cpp`, not unrelated standalone display
implementations. The existing 24-scan target, double-buffer admission and normal
transition waveform remain unchanged.

## Behavior

- Observe changes in requested pixel values while building normal drive rows.
  Identical resubmissions do not reset the quiet timer. Apps that stop submitting
  while stationary also receive cleanup from the continuing scan task.
- Wait 500ms after the most recently observed target change.
- On each eligible scan, select one of sixteen spatially interleaved groups:
  `(x mod 4, y mod 4)`. Only one group receives optional reinforcement.
- Add one pulse toward each selected pixel's existing endpoint. Never replace a
  normal transition command, reinforce an unsettled pixel, or drive away from
  the requested endpoint.
- Stop after two rounds (32 scan opportunities), limiting each eligible pixel
  to at most two supplemental pulses per uninterrupted quiet interval. No
  continuous overdrive while the image remains static.
- A target change resets the delay and cancels remaining cleanup for that scan
  as soon as normal row processing observes it. Earlier unchanged rows in an
  already-running scan may already have received their endpoint pulse.
- Cleanup does not set `g_drive_pending`, own the backbuffer, queue a frame, or
  extend `wait_video_idle`. A new submitted target retains normal admission and
  scan-boundary adoption; it never waits for the cleanup cycle to finish.

There is no full-screen white erase, invert, gray-mode switch, or rewrite cycle.
The pulse command targets existing black pixels with black drive and existing
white pixels with white drive. Scan rows not needing normal work or selected
cleanup continue using the shared blank row. Only a small scheduler object and
existing DMA/state buffers are used; there is no additional framebuffer or
per-scan allocation.

## Dithered monochrome versus native grayscale

Every physical pixel in a dithered image is a black/white endpoint, so all its
pixels are eligible after normal settling. In native 2bpp grayscale, only levels
0 (white) and 3 (black) receive idle reinforcement. Levels 1 and 2 are deliberately
unchanged: additional one-way pulses could bias their shade. A calibrated
mid-gray maintenance waveform is not provided by this change.

Endpoint reinforcement leaves logical commanded state unchanged. State tracks
commands, not the physical panel response. The supplemental dose is bounded,
but neither code inspection nor a host preview can prove ghost removal or the
absence of visible artifacts on the physical panel. This is an incremental
cleanup mechanism requiring optical validation on the device.

## Verification

`test/native_apps/native_video_idle_test.cpp` verifies:

- Quiet delay, active-motion postponement, identical-frame progress, finite
  cleanup dose, rearming after changes, and millisecond counter wraparound.
- Exact 4x4 spatial coverage and the direction of monochrome endpoint commands.
- Preservation of normal transition commands and pixel state.
- Exclusion of unknown/unsettled states and intermediate gray levels.
- Requested-target change detection and unchanged frame-admission rules.

The tests use the same scheduler and row helper as production. They run under
ASan/UBSan (LeakSanitizer disabled in the ptrace-based execution environment).
Existing gray transition/startup/interruption tests also pass. Both tests are
included in `test/run_native_app_test.sh`. The T5S3 PRO firmware build passes;
physical cleanup quality and motion-resume appearance are not yet measured.

Firmware version is **1.3.30 → 1.3.31**, above the checked master and published
release-index version. App versions do not change because their payloads and
APIs are unchanged.
