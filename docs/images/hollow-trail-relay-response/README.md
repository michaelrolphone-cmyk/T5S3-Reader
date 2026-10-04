# City relay: actual before/after response

Before is PR411's first published/tested head `22a6451bd30fafb8adca0b741b3d71e69a37536b`.
After is the same ongoing PR's relay-response continuation. Both are part of the
same unreleased Hollow Trail 1.1.48 increment. Exact app-source SHA-256 values and
frame hashes for both sides are in [comparison metadata](comparison-metadata.json).

- [Player-restored relay, normal close view](powered-before-after.png)
- [Lower-window reaction composition](reaction-before-after.png)

Every comparison retains full 960×540 grayscale above and actual packed mono
below, without image scaling. Its copied regions were verified byte-for-byte
against their original frames. The unsolved-control grayscale and mono frames
are byte-identical before/after. The changed lights are the existing windows
below the last roof, not a new floating overlay or an unrelated signal.

For the reaction pair, the old source is rendered at the same lower, wider
camera using its production drawing functions; the new source is the actual
player-earned reaction timeline at tick240. The camera fixture is explicitly
recorded in metadata. The new captions quote the novella. The old side has the
ordinary chapter narration because no relay reaction existed there.

Reproduce with a host C compiler and Pillow:

```sh
python scripts/preview_hollow_trail_relay.py --source-root ../hollow-relay-before --output dist/relay-before
python scripts/preview_hollow_trail_relay.py --output dist/relay-after --compare-before dist/relay-before
```

The before checkout is pinned to the commit above. The generator retains all
individual originals in the chosen output directories. These are real host
rasters, not device photographs; no physical contrast, ghosting or FPS claim.
