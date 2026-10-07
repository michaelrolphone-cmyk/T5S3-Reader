# Physical signal-window entry: before and after

Original local capture evidence, delivered through PR414.

Before: local bedside checkpoint `3ad3ff07d69d`.
After: local window source `2fd319ce582dff860b22a44cb71cc4a0a431d387`.
Both carry the cumulative unreleased app version 1.1.49. Full verified commit
identities and source hashes are in the capture metadata, and each image caption
identifies the compared source rather than relying on version alone.

Every comparison preserves the two full 960×540 production grayscale rasters,
then the two packed-monochrome rasters. Matching fixed framing makes the window
geometry comparable. Original captures and byte hashes are retained alongside
the comparisons. These are actual software render outputs, not hardware captures.

- [Approach and closed stop](after/approach-before-after.png)
- [Palm and finger marks in the flash](after/handprint-before-after.png)
- [The same glass during the dark interval](after/dark-glass-before-after.png)
- [Opened casements before stepping up](after/open-before-after.png)
- [Feet on the shared sill](after/sill-before-after.png)
- [Inside, back on the unchanged roof](after/inside-before-after.png)

Reproduce from this source using the preview script with --source-root pointing
to the matching checkout and --source-ref to the exact source commit.

Captured source IDs above are the original local build commits, not GitHub
publication commit IDs. See [published source and provenance](../../HOLLOW_TRAIL_PUBLICATION_1_1_49.md).
