# Living bed: leaf and warm soil

Chapter VII's first arrival now has a tended bed on the existing support before
the movable crate. Its strings, shallow root channels, can on bricks and broken
pot sheltering a shoot are grounded in the novella. The player may kneel, touch
the leaf, see the pale quarry-dust crescent, wet a finger and clean it away,
watch the leaf rise, then rest a palm on the warm soil. Quiet player-controlled
holds keep the small response visible. The held leaf insert uses the production
renderer and shows the crescent clearly in packed monochrome.

The kneeling body, leaf contact and soil contact share authored world coordinates.
Arm and leg reach are checked through every transition. Pause, archive, controller
loss, cancellation, retry and exit use the established controls. The scene freezes
normal play and returns the original state, including the movable crate, mirror
settings, route and evidence. It adds no collectible or solution requirement.

Actual captures compare source `ac457680f` to `e428a1009`, both unreleased 1.1.49,
using `scripts/preview_hollow_trail_bed.py`. Nine pairs retain byte-identical source
rasters in grayscale and packed monochrome. Local captures are in `dist/bed-before`
and `dist/bed-final-after`; PR414 merged without this local increment.

The post-merge continuation reconciles these three commits without source conflicts
onto master `c765a9931e2f1772d1dd3b5870362c18a08a9e57`. The app advances
**1.1.49 → 1.1.50**, with minimum firmware retained at **1.3.37**. The live
release lineage was checked after the 1.1.49 release at 06:57 UTC on 2026-10-06;
no 1.1.50 tag or open app-version reservation was found. Fresh source
`641be8cc46ee` captures preserve the original before frames and identify 1.1.50
in all nine new captions. [Inspect the post-merge comparisons](images/hollow-trail-living-bed/README.md).

Focused ASan/UBSan interaction/contact tests, C++ math, package version and S3
ELF/package checks pass. Only the first glasshouse baseline reference changes;
the other 29 baseline references remain exact. The full native aggregate passed
with exit 0 on `64779ecd4`; after the final curved-rib drawing correction in
`e428a1009`, the full production-renderer sanitizer test, focused interaction
sanitizer test and S3 package build passed again.

S3 ELF: 394,640 bytes, SHA-256
`247327300283d8131286ab5790e8ffd2d7acb8bc05dcee1501f7214d2a457343`.

The post-merge 1.1.50 full native aggregate also completed with exit 0, including
ASan/UBSan, full production renderer, all original grotto views, route/ending
guards and C++ math. The 1.1.50 sidecar, package manifest and ZIP catalog agree.

The three-row hillside extent, food/drink, evening tray/stem care, aligned garden
counts and night rest remain separate gaps. This increment does not claim
whole-book completion or device-panel validation.
