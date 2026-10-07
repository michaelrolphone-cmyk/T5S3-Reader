# Chapter X: settlement streets and ordinary traces

Novella lines 1534–1574 and playable scene 41 now have a continuous scenery
segment along level nine's existing street approach. Rough retaining walls,
recessed alley steps, overlapping wall corners and unequal roof heights conceal
parts of the next frontage. A swept closed doorway sits beside a leaf-drifted
one. The empty wash line retains seven clothespins; a stone holds a shallow
upside-down bowl; the cleared boot scraper has its own shaped mud heap. A dead
vine leans on a bent trellis beside a freshly turned bed, followed by a stone
cistern and a covered roof-water channel.

These are physical traces, not an answer about the houses. The panes remain
clouded and reflected. There are no added people, replies, evidence bits,
interaction modes, captions, occupancy verdicts or puzzle conditions. The
existing first-house reflected-doorpost, bare-knuckle knock and unanswered wait
remain intact.

## Actual route and support coordinates

Coordinates are world-space logical pixels, with positive y downward. Floors
come from the same `ht_surface_at` samples used by the live route. The scenery
renders behind the protagonist and moving crate, with opaque near wall corners
covering deeper alley details. It creates no new collision or traversable route.

| Feature | Support | World coordinates / floor |
| --- | --- | --- |
| Existing first house | 0 | x270, floor y220 |
| Retaining wall and recessed side steps | 0 | x347–420, floor y220; stop before the x430–454 gap |
| Unequal houses / recessed alley | 1 | x552–741, floor y180; alley x623–647 |
| Wash line | 1 wall attachments | (557,119) to (733,127), with a shallow sag and seven pins |
| Weighted inverted bowl | 1 | rim centered x590/y179, support y180; stone top y168 |
| Cleared boot scraper | 1 | x697–709, blade y173; mud heap x714–727, base y179 |
| Dead-vine bed / newly turned bed | 2 | x798–852 / x865–921, backed by the ledge above floor y180 |
| Cistern | 2 | x951–987, body base y180, rim y151 |
| First-window cable entry | 0 house | (284,181), clear of the movable reflection channel and sill moth |
| Second-window cable entry | 1 house | (716,138) |
| Lamp-terrace inward returns | 9 | original controls x2900/2985/3070, floor y80; return rail y72 |

The exterior cable descends from (1008,106), follows the street walls via
(949,135), (775,135), (744,125), (554,125), (500,137), (432,165) and
(341,165), then turns at (341,181) into the first window. Clips and arrows are
visible on the same line. At the original lamp terrace its other visible end
joins the existing feed, with arrows pointing toward the houses. The intervening
run is concealed by the tower: no unsupported cable span or new walkway is
invented across its channel. The existing distant mast and houses are retained.

The x430–454 and x760–770 street gaps remain uncovered. The crate at x530,
ladders, hook landing, channel boat, upper radio card, three separate left-lamp
presses, cabinet choices and submitted-final-page door guards are unchanged.

## Verification

The focused scenery test checks visible packed-monochrome pixels for the bowl,
stone, scraper/heap, clothespins, vine/bed, cistern, recessed alley and inward
returns. It covers baseline, full-native and half-height native rasters at both
normal and close framing; repeated frozen rendering, live-state immutability,
unchanged street gaps and exclusion from other chapters / final-door states.
It is wired into the existing native-app runner.

First-house and tower-route checks retain the real HID/XInput controller,
reading, reflection, knock/wait, boat, signal and final-choice guards. The
production renderer has precisely two changed reference pairs: the first two
settlement views. The remaining 28 pairs are exact. Strict C++ engine/math and
S3 compilation, ELF structural validation, ordinary package staging and ZIP
release-record validation are checked with the focused logs.

This belongs to the cumulative unreleased **1.1.49 → 1.1.50** app update;
minimum firmware remains **1.3.37**. The supplied S3 compiler is reused; all build outputs remain in `/tmp`. New screenshots, galleries and
uploads are waived; previous evidence remains untouched. There is no hardware,
FPS, full-repository aggregate, remote publication, merge, release or flash
claim. Whole-book parity remains incomplete.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
Source, focused tests and package are preserved in the delivered checkpoint
archive. New screenshot deliverables are temporarily deferred at the owner’s
request; existing captured evidence remains intact. The PR records the exact
status of previously published and held media.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
