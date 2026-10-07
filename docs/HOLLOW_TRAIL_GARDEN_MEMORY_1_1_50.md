# Chapter VII: the face remembered from the far pallet

Local app-source commit: `97bc923a87c70b2ffc3fa209874461f32bf681f1`.
Exact parent: `b8a9851c141c87e81fab0eb857eac60506d20ed7`.
This remains the cumulative unreleased **1.1.50** update, with minimum firmware
**1.3.37**. No remote write, publication, merge, release or device action occurred.

## Connected memory and unchanged player choice

The existing optional farthest-pallet rest owns one bounded recollection. After
settling, four seconds of the held night precede a roughly twenty-five-second
sequence: the sister asleep with her folded hand beneath her cheek, the crease
fading as she makes tea, the younger grass face, the tired doorway face sliding
across it, and the patient carriage face. Exact excerpts from Chapter VII supply
the captions. The memory leaves the uncertainty intact.

The head turns onto the pillow using the same asymmetrical profile as the later
faces. Folded knuckles support the cheek. The tea pot follows its hand; the brief
pour meets the cup. The crease fades continuously and disappears in both the gray
raster and production packed monochrome. A quieter younger contour slides behind
the tired face. All drawing is bounded integer geometry, without an allocation,
new texture, frame buffer, gameplay room or evidence flag.

The memory counter saturates and the picture returns to the original held night.
A/Confirm still chooses morning at any point, including during the memory; X/Back
returns to play. Pause, notes, host exit, controller loss and neutral recovery
retain the original rest controls. Replaying a newly chosen rest permits the
memory again. The original player-chosen morning and standing stages remain.
Ordinary game state is frozen throughout: position, camera, physics, crate,
mirrors, evidence, route, reading guards and ending conditions are untouched.

## Actual source-labelled evidence

`Hollow-garden-sister-memory-evidence.zip` preserves fourteen before/after pairs,
including native 960 x 540 gray and production packed mono originals. Before is
the exact parent above; after is source `97bc923a87c7`. Both fixtures walk the
actual chapter route to the existing pallet, choose the same rest and use the
same ordinary input/timing. Before stays in its quiet held night while after
shows the new memory. No evidence or puzzle progress is injected.

Source-file hashes, both verified commits, all 56 original raster hashes and
comparison-panel pixel regions are checked. Outside, chosen-rest, morning,
awake and returned images are byte-identical to their corresponding baseline
raster in both modes. The quiet-night return differs only in its caption.
Actual pixel inspection covered the contact sheet and full-size packed-mono
folded hand, crease, pour, sliding faces and carriage. The first review found an
upright sleeping head; the final source turns it onto the pillow and joins the
neck to the supported shoulder before evidence was recaptured.

## Local validation and limits

Passed:
- Strict focused ASan/UBSan checks: real approach/rest, one bounded memory,
  interrupted and completed return, crease pixels, hand/prop reach, deterministic
  gray/mono rendering and byte-identical live gameplay state.
- XInput and HID at every memory cue: pause, notes, controller failure, held-input
  recovery, wake, cancel, replay and host exit. The original night/contact tests
  also pass and check the additional clock field.
- Existing complete ten-chapter route, all thirty production renderer reference
  pairs, controls, ending/read guards, counts, drawings, partition, care, food,
  render pipeline and cooperative render-service checks, with ASan/UBSan.
- Strict C++ math/renderer integration, exact novella-caption excerpts,
  capture-identity checks and changed-package-version comparison with the local
  `origin/master` snapshot. No new remote version claim is made.
- Final S3 ELF structural/import validation, ordinary bundle export, offline ZIP
  record validation, identical packaged ELF/sidecar bytes, hashes and agreement
  of source, sidecar, manifest and both catalogs.

The initial strict compile exposed missing zero initializers in the old night
fixture after its state gained the memory clock; those fixtures were corrected.
Leak scanning was disabled for the container, while ASan and UBSan remained on.
The first final bundle export correctly refused its nonempty output directory;
that earlier output was preserved, and export to the fresh location then passed.
The local Git author was unset; the source commit used the existing repository
checkpoint identity for that command only.

Final ELF: **437,356 bytes**, SHA-256
`8228f22f038314f9661e4928ec32f45b7d1bc47a7b037c9fd87c864ccb844fbf`.
Final bundle: **438,579 bytes**, SHA-256
`2cd38c0f40bb9db51fae5a21de41d8984a3bad3bffa531be608a6b5bef09f606`.

The full native-app aggregate and integration with the concurrent privacy work
belong to the parent task and have **not** been run here. No device appearance,
FPS, release qualification or whole-book completion is claimed. Existing
publication blocks remain in force; this increment performs no alternate write.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
The complete source-labelled scene captures, focused tests and package are
preserved in the delivered evidence archive. GitHub image publication is
still in progress and may be partial; the PR records its exact status.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
