# Chapter III: the blank list, notebook and released feed

The existing paired-list inspection now connects the next physical gestures
from the novella's distribution passage. B or Up stays with the lists: a short
bounded gust lifts only the outer sheet with its empty eighth line. Its hand
lags behind the rising corner, catches it, and settles into a quiet hold. The
inner marked list stays on the other knee. A restrained view beyond the tanks
shows the rail embankment while the narrator stays sitting.

Further deliberate B/Up presses lay the lists against the low wall, take out
the notebook, copy the two dates and initial, cross the initial out, then rub
the crossing until the paper pills.
The rendered date strokes have no calendar numerals, and the initial remains
an indistinct mark, as in the paired lists. No name, calendar date or puzzle
answer is invented. A and Start retain their existing reading behavior. Left/Right
still compare the sheets; B/Up from the last hold returns to that comparison.
X returns to the same frozen game position. Every gesture has a fixed bound
(96–160 ticks at 32 ms), ending in an indefinite player-controlled hold.
Pause, reading, controller failure/recovery, cancel, reinspection and host exit
remain available. None grants evidence, marks a page read, or changes a puzzle.
The evidence identity and complete original reading passage are unchanged.

The existing player-operated 8/5/3 vessels and separate release lever are
unchanged. Service lamps along low tank bodies now derive their brightness
from the actual released 4/4/0 feed and its existing 0–48 opening progression.
They settle in sequence and then remain steady. Balanced but unreleased
vessels, failed releases and inspection alone leave the lamps dark. There is
no separate success flag, answer hint, automatic transfer or forced camera
cut. All tanks and lamps are scenery on the existing final bank, without new
contact surfaces. The player continues toward the railway on the same route.

## Version and verification

This is part of the same open PR436 update: **Hollow Trail 1.1.49 → 1.1.50**,
minimum firmware **1.3.37**. PR436 was verified open at `b3f904272f2d`; the
master app remained 1.1.49. No additional independently published version is
created by this local continuation.

Focused ASan/UBSan tests cover the original comparison and all new gestures,
HID/XInput/mapped controls, pause/read/fault/retry/exit, hand-to-sheet contact,
quiet stable rasters, real transfer conservation/release validation, and
individual lamp response in grayscale and production packed monochrome.
The complete route, all 30 existing renderer pairs, all 300 original grotto
references and strict C++ math checks pass. The S3 ELF structural/import check,
ordinary package export, offline ZIP validation, changed-app version guard and
source/sidecar/package/catalog version agreement pass. The final integrated
aggregate and remote CI remain for integration.
Local host images and package checks do not claim panel appearance or FPS.

Reproduce the source-labelled before/after grayscale and packed-mono evidence
with `scripts/preview_hollow_trail_distribution_story.py`. The baseline is local
source `210234dc0a269c3e31b43851afc065a69566ac4b`. Both versions walk the real
route and operate the production transfer controls; the unreleased 4/4/0
frame is captured before operating the feed. The baseline truthfully remains
in its paired-list hold where the new gestures did not exist. Feed views use
matching render-only framing. Every full-size comparison retains the original
960×540 pixels, and metadata records each source file hash and observed state.

The broader whole-book alignment remains ongoing.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
The complete source-labelled scene captures, focused tests and package are
preserved in the delivered evidence archive. GitHub image publication is
still in progress and may be partial; the PR records its exact status.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
