# Broken-shutter entry: production renderer comparisons

Each composite contains baseline grayscale at upper left, current grayscale at
upper right, and the corresponding production packed monochrome directly below.
All scene panels are unscaled 960×540 pixels. The comparison script verifies the
copied raster regions byte-for-byte; no generative images are used.

- [Approach](after/approach-before-after.png): existing grounded descent and the opened downhill shutter
- [Contact](after/contact-before-after.png): player-directed jamb contact before stepping up
- [Sill](after/sill-before-after.png): feet on the physical sill, hands crossing the jambs
- [Interior](after/inside-before-after.png): grounded release beside the unchanged desk

Original pixel hashes and provenance are in [before](before/capture-metadata.json)
and [after](after/capture-metadata.json). Every original grayscale/mono raster is
preserved at full size in its comparison; separate files can be regenerated
with the script. [Comparison metadata](after/comparison-metadata.json)
contains per-view changed-pixel counts and source hashes. Both sources use the
same provisional app version, 1.1.49; they are commits in one unreleased update.

Baseline local commit: `78d28e27`. Its app source matches the public checkpoint
[`f42a82d71e4df400ed83a346cbd7bba08c812ef4`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/commit/f42a82d71e4df400ed83a346cbd7bba08c812ef4).
The baseline follows ordinary walking through the formerly visual-only timber
face. The after captures approach with 40 Right physics ticks, take hold with A,
and use 16/48/96 Right ticks for contact/crossing/inside respectively. No fixture
moves the character, desk, evidence, checkpoint or animation phase afterward.
Camera framing and the weather tick are fixed for capture comparison.

Reproduce with the committed script and Pillow:

```sh
python3 scripts/preview_hollow_trail_shutter.py --source-root /path/to/baseline --output /tmp/shutter-before
python3 scripts/preview_hollow_trail_shutter.py --output /tmp/shutter-after --compare-before /tmp/shutter-before
```

These are inspected host renderer captures. They do not establish device FPS,
ghosting, display quality or hardware qualification.

Comparison captions identify both the app version and the verified source commit.
The shared 1.1.49 version is one unreleased update containing multiple source
checkpoints, not a claim that both pictures show identical code.
