# Chapter VII: water, food and the cupboard note

The optional first-house interaction now shows the sweating warm return, barrel,
row of cups, a slow tap drop and its mineral mound. The player fills a cup,
drinks, waits, then deliberately drinks again. Under the workbench are a sealed
bean jar, hard biscuits and roots in sand. One biscuit is taken from a reachable
shelf and disappears at the same hand contact; the first bite, hurried eating
and standing recovery follow. The list is on the inside cupboard door, with the
book's actual line: “Keep enough to begin again.” The door closes slowly, then
the character walks back before ordinary control resumes.

## Route and controls

The first-house floor is the existing level-6 platform 0: terrain becomes flat
at X250 and stays at Y220 through the work area. The living bed stays at X232.
The crate starts at X330 and still moves toward X452 for the step to the upper
house at X480. The food approach is X373, reached through an actual jump past
the original crate. Furniture is behind the walking strip, with no new solid
or route edit. A crate left across the staged movement corridor suppresses this
optional prompt rather than moving it or putting the character through it.

The entire game state remains frozen during inspection, including the crate,
mirror solution, evidence, scene clocks and original player position. No new
collectible, healing meter or required task is introduced. Pause, archive,
controller loss, neutral rearm, cancellation, replay and host exit follow the
existing controls. The note requires no calendar date or inferred identity.

## Evidence and checks

[Thirteen actual before/after comparisons](images/hollow-trail-food/README.md)
retain unscaled 960×540 gray and packed-monochrome output. Before app source is
`24d5c40f9bba1211f64a14d51937cfcd113c9cce`; after app source is
`d14d7c4671af331704c50701f7c38dbd804a1320`. Source labels and all file hashes are
verified. The before build stays in ordinary play at the same position.

Pixel inspection covered the source-labelled composites, readable inner-door
note in packed monochrome, sweating return, single falling drop, mineral mound,
cup/hand and biscuit/shelf contacts, and the closed-door return. The first
reference view changes because the new cups/barrel enter its right edge. That
one reviewed grayscale/monochrome golden pair is updated; all 29 others remain
exact. A narrow food-layout correction moved the biscuit to its physical hand
contact and removed it after pickup before final capture/package validation.

Passed:
- Focused ASan/UBSan scene controls, every-stage interruption/retry, pose reach,
  exact game-state retention, slow-drop visibility and both raster modes.
- Input-only continuation through the original crate step, all three chapter
  evidence sites and the unchanged two-stage mirror/vent route.
- Existing bed, night, counts, drawings, controls, endings and partition
  sanitizer tests; all 30 production-renderer reference pairs and native/camera
  contracts; strict C++ math; source-identity and book-wording checks.
- Changed-app version guard against `origin/master`.
- ESP32-S3 ELF build/structural checks, ordinary ZIP export/offline validation,
  source/sidecar/package/catalog version agreement and identical packaged bytes.

The app remains cumulative **1.1.50**, minimum firmware **1.3.37**. Final ELF:
422,928 bytes, SHA-256
`be46adf86ebba168e71dc0e7acc8d60f646a2da46c85dd3478c29399276b8f51`.
ZIP: 424,151 bytes, SHA-256
`080ce17c026e959619e136b89c7bfd69c9c527f00c392fc6950fc756e3344561`.
An initial repeat ZIP export correctly refused a nonempty output directory;
that previous output was preserved separately before the final successful build.

The integrated full native aggregate is tracked with the PR. No device run,
panel/FPS measurement, merge or release is claimed by this increment. The [evening tending](HOLLOW_TRAIL_CARE_1_1_50.md) is now connected. The towel/comb privacy gesture,
sister-face/crease memory and broader hillside continuity remain book gaps.
