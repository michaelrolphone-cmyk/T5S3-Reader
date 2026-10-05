# The westward view: actual before and after

Before: published desk increment `213113ffaf91db1a55f8c7cebac3b64f7126dae7`.
After: source checkpoint `f42a82d71e4df400ed83a346cbd7bba08c812ef4`.
The metadata records exact app-source and original-raster SHA-256 values.

- [Country and pumpjacks](country-before-after.png)
- [Warehouse yards and sidings](yards-before-after.png)

The prior source has no westward shot, so the before side shows the real reached
roof. The new side is the production earned timeline after the watch log.
Both full-size 960×540 grayscale and packed monochrome are preserved in each
comparison; original image regions are verified byte-for-byte. The unchanged
departure and return roof controls match their baseline frames, including actual
nonzero camera rotation. These are host game rasters, not display photographs.

```sh
python scripts/preview_hollow_trail_western.py --source-root /path/to/213113ff --output /tmp/west-before
python scripts/preview_hollow_trail_western.py --source-root /path/to/f42a82d7 --output /tmp/west-after --compare-before /tmp/west-before
```

The looked-at country has a curved pumpjack horsehead, crank/counterweight and
polish rod, with the stopped mechanisms kept small against distant bluffs. The
near warehouse and cornice edges retain their higher contrast in both rasters.

Comparison captions identify both the app version and the verified source commit.
The shared 1.1.49 version is one unreleased update containing multiple source
checkpoints, not a claim that both pictures show identical code.
