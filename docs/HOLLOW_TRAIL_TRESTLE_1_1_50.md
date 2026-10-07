# Under the railway, cumulative 1.1.50

Chapter IV now crosses the actual lower bracing between the station and the
existing service rope. The supported footprint remains x1240–1500: the lower
member is x1240–1480 at y268, and the original far cap is x1480–1500 at y220.
The former uninterrupted upper walking deck is removed. Recessed rails and
unequal sleeper ends remain overhead, with wet bolts, steel triangles, the open
span and broken timbers resting in the ditch below. The old decorative bench
on this member is removed. No support extends across the x1500–1600 rope gap.

A takes the brace from the station edge, either end, or the grounded lower
member. Left/Right controls a continuous, reversible descent, crossing and
ascent. The actor moves beside the exposed earth and far cap; their collision
remains active throughout. The visible member and cap are also the ordinary
collision surfaces. A player who steps off the edge lands on the lower member
and can take the brace there. Checkpoint retry spawns on that same real member.

Halfway across, the forward hand reaches the wool wound around a splintered
brace. Progress waits for a fresh A to test the next footing. The leading toe
reaches and plants before the body follows; the other foot stays planted. The
wool remains wound on its brace and is never raised to the face or collected.
Neutral input holds the pose and Left returns at any point. Actual grip travel
leaves a bounded row of diminishing pale palm prints. Book-derived captions
follow the physical action and remain held when the player stops.

The traversal uses 442 bounded phases, advances only with directional input,
and restores ordinary movement beside the unchanged rope anchor. A/B do not
detach a traveller between landings. Pause, journal and raw-controller faults
hold the crossing; recovered input must return to neutral. Focused testing also
fixed an elapsed-tick edge where a failing or changed controller could consume
one previous held movement before rearming. That change is scoped to this
precision traversal. Host exit remains available.

The rope anchor (1494,112), length 84, gap, intact locomotive/carriages, original
cabin evidence at x1855, map/paper-dog interaction and shunting puzzle are retained.
The input-only ten-chapter witness now deliberately uses the bracing before the
same rope and cabin route. No evidence, puzzle solution or new story conclusion
is granted by taking the brace.

This belongs to PR436's cumulative unreleased Hollow Trail 1.1.49 → 1.1.50 update;
minimum firmware remains 1.3.37. The PR was verified open, with master at
`c765a9931e2f1772d1dd3b5870362c18a08a9e57` (app 1.1.49). Source/package identity is
checked against that target. This is not whole-book completion or a release.

## Reproduction and evidence

Run `scripts/preview_hollow_trail_trestle.py` with `--output`, optional
`--source-root`/`--source-ref` and `--compare-before`. It walks the real rail
approach and uses production input, simulation, drawing and monochrome packing.
The before source `6021fda736` has no lower route: ordinary input reaches matching
world-x positions on its original upper deck. It does not invent missing before
art. Both sides use the same fixed world framing.

Ten source-labelled comparisons preserve original 960×540 grayscale and packed
monochrome panels byte-for-byte. Capture metadata includes app-source hashes,
actual body/mode/evidence state and per-panel pixel hashes. The bounded local
evidence archive contains all original captures and comparisons; parent
integration owns publication. These are software rasters, not panel photographs,
FPS measurements or hardware qualification.

Focused contact/controller tests, full route, ground integrity, character,
controller/read/ending guards, cabin/carriage regression and strict C++ math
checks accompany the source. The renderer changes only the third rail view;
the other 29 golden pairs remain exact, including boat and dam scenes. The S3
ELF structural/import checks and ordinary ZIP/sidecar/catalog identity checks
are included with the final bounded handoff. Parent integration owns the final
combined native-app aggregate and remote checks.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
The complete source-labelled scene captures, focused tests and package are
preserved in the delivered evidence archive. GitHub image publication is
still in progress and may be partial; the PR records its exact status.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
