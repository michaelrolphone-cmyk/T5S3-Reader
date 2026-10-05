# Chapter IV: HOME in the station

The book places HOME on the station noticeboard, with the cancellation book
below and an earlier weather warning above. The former game placed its ticket
evidence at x662 beside the locomotive and described it as inside a carriage.

The interaction now belongs at x1180 on existing rail terrace 2. The small
station occupies x1124–1235: after the intact locomotive/two carriages and before
the trestle at x1240. Its benches, board, warning and book meet the same route
floor. The train bodies, wheels, cab-step plant, trestle, terrain and shunting
controls retain their existing geometry and behavior. Old duplicate ticket
scraps on generic trackside benches are removed.

## Physical inspection

A at HOME opens comparison. Left/Right selects the ticket, cancellation book,
weather warning and the blocked-cutting sightline. The documents keep their
relative positions; selection changes focus rather than manufacturing a new
document. Holding A smooths the ticket crease with a palm. Releasing it restores
the fold. The punched hole exposes the board behind the ticket.

The ordering is exactly the book's: weather warning before cancellation,
cancellation before collapse. No numerical/calendar date is invented. The
close documents carry handwriting strokes; the observable ordering is stated
in the inspection captions and exact journal passage.

The cutting is on the abandoned parallel track ahead of the locomotive's
forward end. A pale exposed scar, fallen tree/roots, debris and descending
telegraph wire show why the rails do not continue. This is background geography,
not an obstruction added to the foreground walking route. The look-back view
uses those same world coordinates and train drawing.

Start reads the exact station passage, preserving comparison focus for return.
Pause freezes inspection; X/Back cancels with neutral gating. Reinspection resets
the comparison. Host exit and controller fault/recovery remain available. No
gameplay actor, evidence beyond the normal inspection, puzzle or camera state
is changed by viewing the documents. A stationary comparison does not request
continuous scene redraws.

The station location is corrected in narration and journal text. The book's
actual passage replaces the old abbreviated ticket account, and shunting
guidance remains separately available. The wheel-less sleeping carriage, dog
drawing and other Chapter IV gaps remain separate work.

## Actual evidence

[Before/after comparisons](images/hollow-trail-station-ticket/README.md) follow
the input-only rail route. Exterior views use matching framing. The old ticket
opened a journal; comparisons retain that genuine baseline against the new
physical inspection. Original grayscale/mono outputs and verified source
identities are retained. These are software captures, not hardware photographs.

This is a separate local checkpoint after the stove shed. Publication remains
paused. The cumulative unreleased app version is still **1.1.48 → 1.1.49**,
with minimum firmware **1.3.37** unchanged.

## Verified local checkpoint

The complete native-app aggregate passes on the final source with ASan/UBSan.
Leak detection is disabled solely for this runner's ptrace limitation.

- HID/XInput hold/release, all four comparisons, idle redraw suppression,
  pause, journal return, polling fault/recovery, cancellation, reinspection
  and host exit pass without mutating the gameplay snapshot.
- A crease-only crop excludes the hand and captions: 165 native packed-mono
  pixels change while held (128 in the low-render pack). Release restores the
  original packed frame exactly. Both raster modes are deterministic.
- Exact book passage, station wording, relative chronology, retained shunting
  guidance and bounded journal body pass.
- The complete input-only ten-chapter route still passes.
- Two rail reference views intentionally change for the cutting/station and
  removal of misplaced ticket scraps; the other 28 golden pairs stay exact.
- S3 ELF build, structural validation, offline ZIP/catalog verification,
  changed-app version guard and package hash/version agreement pass.
- Seven source-labelled comparisons preserve their original raster regions
  byte-for-byte.

Source: `f601f8befd144e0a0ed81da76b66e00bf37f019f`.
ELF: 321856 bytes; SHA-256
`71ef558aa235203cb01298ed13e83f478b103390114bfca824f4c0ff0dcfa4f3`.

This checkpoint is unpublished, so it has no hosted CI result. No merge,
release or device operation was performed.
