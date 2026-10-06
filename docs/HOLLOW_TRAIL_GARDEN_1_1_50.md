# Garden continuation after PR414

The new `feat/hollow-trail-garden` branch starts at merged master
`c765a9931e2f1772d1dd3b5870362c18a08a9e57` and reconciles the completed living-bed
implementation (`64779ecd4`, `e428a1009`, `f9f4d0a89`) without source conflicts.
The package advances **1.1.49 → 1.1.50**; minimum firmware remains **1.3.37**.
The live released lineage and open-PR reservations were checked on 2026-10-06,
after the 1.1.49 release at 06:57 UTC. No 1.1.50 reservation was found.

## Implemented

At the existing first glasshouse support, real player input earns kneeling,
dusty leaf contact, cleaning, a held released-leaf view, soil contact and
standing. Strings, root channels, the broken-pot shoot, watering can on bricks
and curved shelter ribs follow Chapter VII. The existing crate, mirror puzzle,
evidence, route, controls and complete game state remain unchanged. All limb
contacts are bounded; pause, archive, input loss, cancel, retry and host exit are
tested. The optional interaction creates no new collectible or solution gate.

## Evidence and validation

- Focused interaction/contact test: ASan/UBSan pass.
- Production capture: nine before/after pairs, with unscaled 960×540 grayscale
  and packed-monochrome rasters checked byte-for-byte. Before source `ac457680f73`
  is the historical 1.1.49 baseline; after source `641be8cc46ee` is 1.1.50.
  All source-file hashes are verified against the integrated source.
- Full native-app aggregate: exit 0, ASan/UBSan enabled (LeakSanitizer disabled
  for the runner ptrace limitation). This includes route/ending/read/controller
  guards, production renderer, all 300 original grotto views, and C++ math.
- S3 ELF and package builder: exit 0. Structural ELF and offline ZIP validation pass.
- Source, ELF sidecar, package manifest and catalog all identify 1.1.50.
- ELF: 394,640 bytes, SHA-256
  `247327300283d8131286ab5790e8ffd2d7acb8bc05dcee1501f7214d2a457343`.

All nine comparisons are preserved locally and in the deliverable package.
Eight GitHub comparisons, source labels and reproduction metadata are included
in the [living-bed gallery](images/hollow-trail-living-bed/README.md). The ninth
returned-state comparison remains in the delivery archive; its GitHub upload
is still blocked. This tree excludes that image. Earlier blocked
cabin/sleeping-house evidence is also excluded and has not been retried.

The whole-book assignment continues. Physical duplicate garden-count alignment and
the child-drawing/older-card comparison, chosen far-pallet rest and
[food/drink](HOLLOW_TRAIL_FOOD_1_1_50.md) are connected in this cumulative update.
[Evening tending](HOLLOW_TRAIL_CARE_1_1_50.md) is also connected.
Privacy/memory beats and broader route scenery remain gaps.
No merge, release or device validation is
claimed by this source checkpoint.

The existing level-nine approach now also carries the [settlement streets and
ordinary household traces](HOLLOW_TRAIL_SETTLEMENT_1_1_50.md), retaining the
first-house interaction and all tower/final-door mechanics.
