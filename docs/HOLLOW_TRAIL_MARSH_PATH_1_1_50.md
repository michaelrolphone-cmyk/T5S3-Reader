# Chapter V: the raised marsh bank and quarry sightline

The novella's narrow bank now reads as a route through lateral water. The new
surface treatment follows the existing Chapter V contour between x1540 and
x2780: roots show in its firm dark lip, a silt edge drops toward the shallows,
and the pool tapers into the bank at each end. The actual x1790–1814 channel
remains open. All contact heights, platform extents, jumps and interactions are
unchanged; the water beside the bank is scenery on a different depth plane.

A continuous 1,024-step cloud cycle changes the same pool from visible submerged
wood and stone to reflected pale sky. It changes their contrast, not their
geometry. It uses the existing simulation clock, with no separate timer or
input state. Old reeds thatch the farther ditch; low islands, drowned stems,
doubled weathered posts and willow roots with caught twigs give intermediate
landmarks. Two closer willows pass across the walking view at a different rate.
The first trunk was moved after actual pixel review so the chair, worn groove,
child's loop and bell-rope contact remain clear in their inspection framing.

One pale quarry mass enters from the right and gradually fills more of the sky
as the player moves toward the existing lock. Its dark seam, terrace shadows and
small rim stems resolve the apparent cloud into a cut hillside. The far edge
moves at one tenth the camera rate, behind the faster willows and posts. The
existing grotto exit finishes first; a short continuous landscape exposure
starts beyond camera x920. No actor threshold, evidence discovery, forced camera
move or new cutscene starts the view.

This connects the scenery in set 21 and the forward sightline toward set 24.
It does not claim set 24's upper-landing/backward vista or a new physical climb.
The current lock route remains the original conserved-water puzzle. The ferry,
grotto, waterfall/dam, awning/bell, evidence and controls retain their mechanics.

## Verification

- The focused ASan/UBSan test follows actual Chapter V inputs, rows the original
  boat, crosses the real bank/gap, collects all three original discoveries,
  operates the lock controls and enters Chapter VI without a death. It checks
  deterministic immutable rendering, cloud continuity/wrap, visible monochrome
  differences at clear/mirror extrema, and an unchanged contact edge in the
  low, native and native half-height drawing paths.
- The complete route, receiver HID/XInput controls, waiting-awning interruption,
  cancellation/retry and silent hold, ground integrity and coherent render
  service tests pass. Existing grotto checks retain all 300 exact reference
  views and ten complete nearest-camera frame hashes.
- Actual before/after inspection approves only renderer reference 14 (Chapter
  V, view 2). Its grayscale/packed-mono hashes become 4025276642 / 2003882676.
  All other 29 pairs remain exact, including every boat and dam reference.
  The complete renderer and supported strict C++ math/engine consumer pass.
- S3 compilation, firmware ELF structural validation, per-ID package staging,
  ZIP export and offline release-record validation pass. App identity stays
  hollow_trail, cumulative unreleased 1.1.49 → 1.1.50 in PR436; the manifest was
  already bumped for this same update. Minimum firmware remains 1.3.37.
- LeakSanitizer cannot run under this executor's ptrace environment. Host checks
  use ASan/UBSan with leak detection disabled; this is not a leak-check result.

## Actual raster evidence

`scripts/preview_hollow_trail_marsh.py` walks the actual Chapter V route to eight
views using its production camera. The clear and mirror captures wait through
ordinary neutral simulation to the two cloud extrema. Every capture compares
source b8aded7064f910a85082ac9ff807333eb2f6b4c9 with source
002b56da796e0acb0083a8e1dfde854d92e8477d. Both are app 1.1.50; source labels,
file hashes and observed body/evidence/camera states disambiguate them.

The retained ferry pair is pixel-identical. The seven intended marsh views show
their exact source-labelled 960×540 grayscale and production packed monochrome
before/after frames. An additional reference comparison reproduces the actual
changed renderer fixture, with its 480×270 grayscale enlarged exactly 2×.
Original comparison pixels are checked byte-for-byte. Separate waiting captures
verify the inspection poses, rope pull, two bell rims and unanswered hold remain
readable against the new scenery.

The bounded `Hollow-marsh-path-evidence.tar.gz` archive contains those captures,
comparison metadata, reference hashes, check logs, changed source/patch and the
built ordinary package, without repository history. No hardware appearance,
panel FPS, remote publication, release qualification or full-repository test
aggregate is claimed.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
Source, focused tests and package are preserved in the delivered checkpoint
archive. New screenshot deliverables are temporarily deferred at the owner’s
request; existing captured evidence remains intact. The PR records the exact
status of previously published and held media.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
