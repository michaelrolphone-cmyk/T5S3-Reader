# Hollow Trail 1.1.49 publication and capture provenance

PR414 contains the cumulative forest, mill, city, final-door, bedside-watch,
physical-window, cold-stove and station-ticket work. This is ongoing book
adaptation, not a claim that all 44 sets are complete.

## Published source

The source-only checkpoint
[e13de1caa1a3efb30232058adb4a32b1493d68ee](https://github.com/michaelrolphone-cmyk/T5S3-Reader/commit/e13de1caa1a3efb30232058adb4a32b1493d68ee)
has verified tree `0b3d1d46d8b29a80595a2f8f0fa4db220c678ad5`.
All Apps, tests and scripts match the final local checkpoint `11f64015`
byte-for-byte. Its hosted [PlatformIO check](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/37304207381)
and [CAM check](https://github.com/michaelrolphone-cmyk/T5S3-Reader/actions/runs/37304207331)
both passed. The following evidence commit adds the retained captures and this
publication record without changing runtime source. Current final-head checks
are recorded in [PR414](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/414).

The bedside source was separately committed as
[21bf23cc](https://github.com/michaelrolphone-cmyk/T5S3-Reader/commit/21bf23cc6119df3e9d797841b6f9eb9a8de2514a)
before the combined source checkpoint.

## Original capture identities

Captions retain the **actual local source commits used when the images were
rendered**. They are not GitHub publication commit IDs, nor separate releases.
The local checkpoints and original image evidence remain preserved.

- Bedside: public baseline b9803323 → local source a87eab01
- Window: local bedside 3ad3ff07 → local source 2fd319ce
- Stove: local window b9689cb1 → local source 8ad776ee
- Station: local stove 0c744278 → local source f601f8be

Each gallery includes full app-file hashes and source verification metadata.
GitHub-created commit metadata changes commit identity; it does not change the
recorded source bytes or make two 1.1.49 snapshots identical. The final combined
source is the published checkpoint above. Every screenshot comparison retains
the original game raster pixels, with grayscale and packed monochrome shown.

[Bedside gallery](images/hollow-trail-watch-memory/README.md) ·
[Window gallery](images/hollow-trail-window-entry/README.md) ·
[Stove gallery](images/hollow-trail-stove-shed/README.md) ·
[Station gallery](images/hollow-trail-station-ticket/README.md)

Version remains one cumulative unreleased **1.1.48 → 1.1.49** update, minimum
firmware **1.3.37**. No merge, release, installation or device operation.
