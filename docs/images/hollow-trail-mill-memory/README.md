# Mill register memory: actual production captures

The same [capture script](../../../scripts/preview_hollow_trail_mill_memory.py)
drives Confirm, Right and Confirm through the real app input path on both
snapshots. The desk moves into the light without teleporting its body. Before
is `213113ff`; after is `f42a82d71e4df400ed83a346cbd7bba08c812ef4`.
Full source and pixel SHA-256 values are in each capture metadata JSON.

- [After contact sheet](after/contact-sheet.png)
- [Register page before/after](after/register-before-after.png)
- [Stable interior before/after](after/interior-before-after.png)
- [Sister writing before/after](after/desk-writing-before-after.png)
- [Wrist/hair gesture before/after](after/wrist-hair-before-after.png)
- [Second loaf before/after](after/second-loaf-before-after.png)
- [Loaf in bag before/after](after/packed-loaf-before-after.png)
- [Departure doorway before/after](after/doorway-before-after.png)
- [Trembling page before/after](after/trembling-page-before-after.png)
- [Covered six names before/after](after/covered-names-before-after.png)
- [Uncovered names before/after](after/uncovered-names-before-after.png)
- [Keeper's final line before/after](after/keeper-line-before-after.png)
- [Full passage journal before/after](after/read-register-before-after.png)

Each comparison has unscaled 960×540 production grayscale on top and the real
packed monochrome underneath. The script verifies that comparison image crops
are byte-identical to their original images. The original pixels are preserved
in these canvases. Individual originals can be reproduced with the script and
their hashes checked against the metadata. Only the after contact sheet is
reduced for overview. It and all full-size grayscale/mono comparisons were
visually inspected during implementation.

The optional separate before contact sheet is not included in the PR. Every
before scene remains available at full resolution in its before/after comparison;
the omitted overview is preserved locally and can be reproduced by the script.

The revised covered-name frame settles the whole hand seven logical pixels higher so it intersects each
of the six lower names; the first name remains fully clear.

The baseline genuinely remains in its old journal throughout these elapsed
ticks, because it had no memory timeline. Its compact lettering is the actual
host fallback when `reader.typography` is absent. The changed last image also
uses that same journal fallback after returning from the memory.

Reproduce from clean checkouts:

```sh
python3 scripts/preview_hollow_trail_mill_memory.py --source-root /path/to/before --source-ref 213113ff --output /tmp/mill-memory-before
python3 scripts/preview_hollow_trail_mill_memory.py --source-root /path/to/after --source-ref f42a82d71e4df400ed83a346cbd7bba08c812ef4 --output /tmp/mill-memory-after --compare-before /tmp/mill-memory-before
```

These are host-rendered frames, not display photographs or a physical device
qualification. No generated image pixels are used.
