# The chosen carriage rest, 1.1.50

Chapter IV's optional wheel-less carriage now connects the sideways window
reflection and household-account dream to the existing broken-window entry,
dry bench, boots, waking extended hand, intact train and dawn reveal.

After choosing the bench, a bounded 352-tick (11.264-second) insert shows the
reclining traveller in the unbroken pane beneath the signal gantry, then turned
toward the seat back. The silent hold waits for a fresh Sleep action. The
704-tick (22.528-second) dream shows her sister in an indoor dress with bare
feet on the seat, successive household-account leaves in the traveller's hand,
and a pointing finger meeting an unmarked cell. Each caption holds for 176
ticks (5.632 seconds). “You'll remember” then remains until a fresh Wake
press resumes the existing extended hand and locomotive reveal.

The scene explicitly remains a dream. It identifies no encounter, answer,
account amounts, new evidence or author. The face and filled-room drawing
motifs match the later garden recollection; a reflected face direction turns
this sister toward the paper. The six existing garden-memory drawings remain
byte-identical in both logical and native rasters.

The sequence lives inside the existing optional carriage interaction and
never alters the live game state. Pause, journal, raw-controller failure,
neutral rearming, Back, host exit and replay work at every new stage. A focused
recovery check exposed the old carriage clock advancing before recovered
input had returned to neutral; the carriage clock now uses the same neutral
hold as newer optional scenes. Entry/exit timber and sill contacts, the route,
shunting puzzle, evidence identities and read/ending guards are preserved.

This is part of PR436's cumulative unreleased 1.1.49 → 1.1.50 change; minimum
firmware remains 1.3.37. PR436 was verified open at source
`d0caa5824bf006b50e73900b435274212de6b169` before finalizing. It is not a new
published version, merge, release or hardware qualification.

## Reproduction and evidence

Run `scripts/preview_hollow_trail_carriage.py --output <directory>` against the
selected source. `--source-root`, `--source-ref` and `--compare-before` support
verified comparisons. Seventeen views include actual 960×540 grayscale and
production packed-mono frames. The missing before-dream frames show the old
silent rest, rather than invented baseline imagery. Every comparison preserves
the original panels byte-for-byte and records source/snapshot hashes.

Pixel review corrected the first sister's face direction toward the account
and strengthened the account rules and handwriting for packed monochrome.
The reflected body remains inside the pane; the blank cell is legible at the
same shared endpoint used by the pointing finger. Unrelated garden images and
the carriage's exterior, window contacts, waking, dawn and returned frames are
checked against their actual before pixels.

Focused ASan/UBSan carriage and garden-memory controls/contact tests, shared
motif pixel checks, cabin/controller/ending guards, complete route, all 30
renderer pairs and strict C++ math checks pass. The S3 app ELF, structural
validator, ordinary package export and offline manifest/catalog version checks
are recorded with the bounded local source/evidence archive. Parent integration
owns the final combined aggregate. No device or panel claim is made.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
The complete source-labelled scene captures, focused tests and package are
preserved in the delivered evidence archive. GitHub image publication is
still in progress and may be partial; the PR records its exact status.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
