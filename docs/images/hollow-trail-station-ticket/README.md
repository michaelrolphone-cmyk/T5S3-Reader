# Station ticket: actual before and after

Original local capture evidence, delivered through PR414.

Before: stove checkpoint `0c744278745e8dcdc4f9f99c7b7951caadf2dc95`.
After: station source `f601f8befd144e0a0ed81da76b66e00bf37f019f`.
Both carry the same cumulative unreleased 1.1.49 version. Captions identify
the actual source SHA; metadata verifies every app-file hash.

- [Station after the intact train, before the trestle](after/outside-before-after.png)
- [HOME ticket and its punched hole/crease](after/ticket-before-after.png)
- [Held palm smooths the crease](after/smoothed-before-after.png)
- [Release restores the crease](after/released-before-after.png)
- [Cancellation book](after/cancellation-before-after.png)
- [Earlier weather warning](after/warning-before-after.png)
- [Same-world blocked cutting and locomotive](after/cutting-before-after.png)

The preview follows the actual rail route and uses Confirm/Left/Right input.
The old ticket opened a journal; its genuine baseline is retained. Exterior
comparisons share fixed framing. The blocked-cutting comparison shows the new
physical sightline, not an invented old hillside scene.

Each comparison contains full 960×540 grayscale and packed-monochrome outputs.
The compact baseline journal uses its real 2× presentation. Every pasted raster
region is checked byte-for-byte against the retained original PNG.
The crease-only mono regression excludes the hand and narration; it verifies
a visible change while held and exact restoration on release.

These are software-rendered captures, not device photographs. The book gives
relative ordering only; no numbered dates have been added.

Captured source IDs above are the original local build commits, not GitHub
publication commit IDs. See [published source and provenance](../../HOLLOW_TRAIL_PUBLICATION_1_1_49.md).
