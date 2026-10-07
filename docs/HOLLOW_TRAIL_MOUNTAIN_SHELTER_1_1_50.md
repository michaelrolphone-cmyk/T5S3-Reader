# Chapter IX: the maintenance shelter below the pass

The opening mountain support now contains a wall-fixed bunk, washbasin,
sparse hooks and a shelf. Its footprint stays within support zero, before the
existing ravine line. A grounded approach near x216 offers an optional rest.
The scene follows novella lines 1348–1370 and playable scene 38: damp clothes
on the bunk, dry stockings pulled over the hands in the coldest part of the
night, turning away from bruised ribs, dark knuckle marks, dawn ice, a finger
breaking it, drinking from underneath, and departure into the snow.

Fresh confirmations begin each movement. Even-numbered stages are quiet
holds; odd stages take 96, 128 or 160 fixed simulation steps. The chosen
overnight rest is one bounded 320-step cutscene: the turn on the bunk, a close
view of the marked cloth, then dawn at the basin. A/Confirm advances or returns
after departure; X/Back returns at any stage. START opens the existing journal
index. There is no new journal page, evidence bit, recovery benefit or route
requirement.

The bunk is fastened to the shelter wall with brackets that end above the
actual sloping floor. The resting body and both feet meet its surface; the
head turns with the torso. The two hands dress one another, with the pulling
fingertips meeting the moving stocking cuff and the loose toes extending past
the fingers. The ice cracks at the finger contact. A cupped hand travels from
that opening to the depicted mouth. Walking to the basin and doorway uses the
existing gait, with stance feet on the real support. The camera-mode snapshot
applies the same scale to the body, basin and room, including no-zoom modes.

All overnight lighting, ice, body motion and camera changes are local render
state. Pause, journal reading, controller faults and held reconnection freeze
the scene clock. Return requires neutral input; cancellation and replay work
throughout the cutscene. Host exit stays immediate. The live position,
checkpoint, world/weather clocks, rope, safety lines, crate, puzzle, evidence,
cabinet, ending and final-door reading fields remain byte-identical. Rendering
uses immutable snapshots and the existing bounded raster checkpoints.

The mountain route witness takes three paths on both HID and XInput: skip,
cancel and complete the rest. Each crosses the original ravine rope, both
safety lines and the crate step, discovers original pages 24–26, isolates the
ridge return and plays the unchanged 1-0-2-1 sequence. Entry into the tower
still has its independent puzzle, testimony and final-door guards.

## Verification

The focused run covers shelter contacts/controls/rendering, the mountain route,
original controls and character behavior, ridge isolator, tower route and final
door. It also checks the existing full route, production renderer and supported
strict C++ engine/math consumer. AddressSanitizer and UndefinedBehaviorSanitizer
are used for the contact/control/route cases; LeakSanitizer is disabled because
the runtime uses ptrace.

The added shelter changes exactly the level-eight opening renderer fixture;
the other 29 gray/packed reference pairs remain exact across all 16 SIMD
readiness masks. The new scene is deterministic in all five camera modes, and
its bunk, stockings, knuckle marks, dawn, broken ice, drink and departure differ
above the captions in gray and production packed monochrome. No existing
gallery or screenshot evidence is removed or replaced.

S3 compilation, firmware ELF structural validation, ordinary per-ID package
staging, ZIP export and offline release-record checks run for the app. Version
remains the cumulative unreleased **1.1.49 → 1.1.50**, minimum firmware
**1.3.37**. The supplied branch already carries that cumulative version bump;
this shelter increment requires no new runtime API.

New screenshots and gallery/upload work are waived for this increment. The
bounded checkpoint contains source changes, tests, commit IDs, logs and the
ordinary package. There is no hardware/FPS qualification, full-repository
aggregate, remote write, merge, release or flash claim. The wider novella map
remains incomplete; the broader vegetation gradient is outside this increment.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
Source, focused tests and package are preserved in the delivered checkpoint
archive. New screenshot deliverables are temporarily deferred at the owner’s
request; existing captured evidence remains intact. The PR records the exact
status of previously published and held media.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
