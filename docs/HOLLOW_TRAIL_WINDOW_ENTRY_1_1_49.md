# Signal-room window entry, cumulative 1.1.49

Chapter II's glass contact and climb now use one physical entry bay on the
existing upper roof. The closed casements stop ordinary walking and jumping.
A takes the window; Left/Right opens and crosses it, with reversal to either
grounded landing. The same frame, sill and lintel coordinates drive collision,
body motion, limb targets and drawing. The ladder, chair, wheel, watch log and
right-hand passage retain their existing positions.

The first deliberate opening step earns the existing signal-room tableau
before the glass opens. Its original duration, camera movement and cues remain.
The palm leaves finger marks that become clearer with the lamp, then lowers
one fingertip at a time. The trace turns away with the opening casement.
Gameplay stays frozen during the tableau and resumes the same attachment.
The player then crosses the sill rather than walking through decorative glass.

Hands grip real reachable jambs and relax between them. Feet lift onto the
actual sill, alternate planted steps and lower to the same roof. Foreground
framing occludes the body; planted toes remain above the sill highlight. Open
casements retain their edge-on stiles and crossbars. The wall below remains
matching plaster beneath a projecting sill, avoiding a doorway-like dark block.
No invisible platform, extra collectible, puzzle solution or room prop is added.

## Controls and interruption

Neutral input holds the attached pose. A/B cannot detach an actor in mid-crossing.
Left reverses; reaching either endpoint restores ordinary traversal. Pause and
journal hold the attachment, and host exit remains live. Checkpoint retry keeps
the opened window and handprint but resets attachment; chapter restart clears
both. The full input-only ten-chapter route crosses the window and still reaches
the log and existing exit.

## Actual raster evidence

The preview uses production physics and render functions, not generated artwork.
It starts on the existing roof at x2040, approaches through ordinary Right
input, attaches with A and advances actual opening/crossing steps. Before uses
the preserved bedside checkpoint. Both sides use matching fixed framing;
the original signal tableau handles the handprint/dark-glass frames.

[Before/after gallery](images/hollow-trail-window-entry/README.md) includes both
960×540 grayscale and packed monochrome rasters, with verified source SHAs.
Comparison image regions are checked byte-for-byte against their original
captures. These are software renders, not device screenshots.

This is part of the same unreleased Hollow Trail **1.1.48 → 1.1.49** update;
minimum firmware remains **1.3.37**. Whole-book work is still incomplete.
This separate local checkpoint follows the bedside memory. Publication remains
pending the previously requested approval; no new remote route was attempted.

## Local verification checkpoint

- Full native-app aggregate passes with ASan/UBSan; leak detection is disabled
  solely for the local runner's ptrace limitation.
- Closed-pane collision, jumping refusal, all forward/reverse hand and foot
  targets, pause, journal, neutral hold, checkpoint retry and restart pass.
- Actual HID/XInput controls, host exit, log handoff and the input-only complete
  ten-chapter route pass. Existing 30 renderer golden pairs remain exact.
- Flash/dark handprint contrast, monochrome visibility, trace disappearing with
  the opened leaf, immutable render state and lossless comparison pixels pass.
- S3 ELF build, structural validation, archive/catalog validation, changed-app
  version guard and ELF/sidecar/archive/catalog identity agreement pass.

Final local source: `2fd319ce582dff860b22a44cb71cc4a0a431d387`.
ELF: 308740 bytes, SHA-256
`55a4daa8ea0a75a78633875ded3abf5982c6882227912cedd97c3172dd14d239`.

No hosted CI has run for this unpublished checkpoint. The earlier public PR
head remains separate; local testing does not imply remote publication.
