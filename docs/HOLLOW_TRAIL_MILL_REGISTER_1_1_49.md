# Hollow Trail 1.1.49: draw the desk into the light

This continues the whole-book adaptation after the owner merged PR411. It is a
Chapter I gameplay increment, not a claim that the novella or every mill beat
is finished. The [44-set gap map](HOLLOW_TRAIL_NOVELLA_ALIGNMENT.md) stays active.

## The book becomes an action

The novella's mill passage has a barred door, broken-shutter entry, a register on
a sloping desk, hard window light and writing lost in shade. The protagonist draws
the desk toward the light; one leg leaves the first fresh scrape through the dust.

- The desk begins in shade at X1128 on the existing mill-hollow ground. A grips
  it; left/right pushes or pulls the actual rigid body. Existing engine/body,
  terrain, input and articulated hand-contact paths are reused.
- The desk has a human-scale 24×14 support frame with its sloping writing board
  behind the front contact edge. Its feet follow the existing contour; the other
  chapters retain their 26-high crates. No new invisible platform or extra arena.
- The register moves with the desk. A can inspect it only while it rests on the
  actual floor in the window beam. Merely approaching the old fixed evidence
  coordinate cannot grant the document. The existing evidence identity and
  journal remain available; inspection releases the desk and stops its inertia.
- A bounded, grounded desk-leg trace records movement in either direction. It
  remains if the desk moves back and survives a checkpoint retry. It does not
  appear before the player moves the desk or follow an airborne body.
- Worn interior floor material follows the same contact contour. Grass and
  drifting leaves stay outside the opened mill interior. The wheel, barred
  doorway, shutter, vegetation, terrain and continuous onward route remain.
- The arrival caption no longer gives away the seven names before the player
  brings the register into the light. It describes the shaded writing instead.

The room walls bound desk travel. Reading and repeated inspection never solve
the chapter transmission. This is ordinary player-operated exploration, not an
automatic desk movement or a replacement for the forest/rope route.

## Actual evidence and checks

[Before/after approach, drawn desk and arrival views](images/hollow-trail-mill-register/README.md)
show the real production raster and packed monochrome. The drawn after-state is
produced by actual grip/movement/inspection calls. Both source snapshots and all
image hashes are recorded; no generated illustration or device qualification.

The new mechanic passes host ASan/UBSan push and pull, grounded contact, dark and
airborne refusal, dynamic evidence, release-on-read, repeat/archive, bounded
travel and persistent-scrape checks. Actual HID/XInput tests cover grip, movement,
reading, frozen state and return/reinspection. The input-only full-book route now
moves the desk normally and still obtains all 30 discoveries without teleporting
bodies or setting mechanism flags. Checkpoint/weather/evidence fixtures explicitly
prepare the lit desk where they test unrelated archive behavior.

One forest-middle grayscale/mono golden pair changes for the worked mill scene.
The other 29 pairs, including the boat scenes, remain exact. The approved reference
change is recorded in the renderer test; no assertion was removed. Full aggregate,
final build and exact-head hosted outcomes are recorded in the PR.

## Scope and remaining work

Hollow Trail **1.1.48 → 1.1.49**; minimum firmware stays 1.3.37. PR411's completed
signal-room and relay increments are preserved. This new branch starts from
master after that merge; the closed PR is not modified.

The sister-at-desk/departure memory is now connected in the
[same unreleased 1.1.49 continuation](HOLLOW_TRAIL_MILL_MEMORY_1_1_49.md).
Explicit broken-shutter entry, other early forest details, the western city vista and street glimpse, later chapter scenes
and the dedicated final door remain on the ongoing map. No merge, release,
installation, device flash, FPS or ghosting qualification is performed here.

The current Xtensa app build, ELF structure, ordinary ZIP export/validation,
changed-source version guard and source/sidecar/package/catalog 1.1.49 agreement
passed. ELF 271112 bytes, SHA-256 `12cc9baf01975d554e38aed9ede1455de44f716365c8d33648cd824a23121f1a`.
Local sanitizer runs disable leak detection for the runner’s ptrace limitation;
CI retains its normal configuration. Physical operation remains unqualified.
