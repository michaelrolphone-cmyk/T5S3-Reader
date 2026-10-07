# Signal room: actual before and after

These are production C-renderer captures, with full 960×540 grayscale and
production-packed monochrome shown side by side. **Before:** merged master
`1d97f4f4050a2f0ec693d5b0f95b27f7be4e49d2`, Hollow Trail 1.1.47. **After:**
source commit `c6cef2870755c7cbe7a90d735f05c8d183e4a296`, Hollow Trail 1.1.48.
The capture harness verified the entire local Apps tree against this published
commit before rendering. Later screenshot/harness changes do not change the app pixels. The individual
metadata files record all app-source SHA-256 hashes and exact raster hashes.
These are host game pixels, not illustrations, device photographs or FPS proof.

## Every worked signal-room view

- [Window approach, flash on](window-lit-before-after.png)
- [Window approach, long pause](window-dark-before-after.png)
- [Actual log/chair location](room-log-before-after.png)
- [Wide composition on the existing upper roof](room-wide-before-after.png)
- [Rain-tank tableau sightline](refuge-sightline-before-after.png)
- [Seated mechanism inspection](study-room-before-after.png)
- [Watch-log pages separated](study-separated-before-after.png)
- [Watch-log pages aligned](study-aligned-before-after.png)
- [Earned signal-room arrival hold](arrival-hold-before-after.png)
- [Signal-room handoff to play](arrival-handoff-before-after.png)
- [Existing journal, now using the novella passage](read-log-before-after.png)

Each comparison's upper row is full grayscale and lower row is packed mono;
no full-frame scaling or invented detail is used. Original images are in
[before](before/) and [after](after/). The two contact sheets alone reduce images
to 480×270 for overview. [Comparison metadata](comparison-metadata.json) records
changed-pixel counts; each side also retains source and output hashes.

The before study is the real existing watch-log journal's compact-lettering
fallback because the host has no reader.typography provider. The after study
shows the new physical inspection reached by the same A action. These are
intentionally different camera modes, not matched-pixel art changes. The journal remains available with A/Start after the new view; its watch-log
prose now directly follows the novella passage, as the read-log pair shows. No claim is made
about a connected typography provider's rendered page.

The approach/log/wide views use identical initial world/camera/tick settings.
The rain-tank pair uses its existing tableau at tick 170 on an initial tick 8
snapshot. The new arrival hold deliberately reframes that same world and animates
the wheel; the baseline shows ordinary gameplay at the corresponding cosmetic
time. Handoff uses the identical retained player state and original lamp phase.

## Reproduce

Use the same current script for both source checkouts:

```sh
python scripts/preview_hollow_trail_signal_room.py --source-root ../baseline --output dist/signal-room-before
python scripts/preview_hollow_trail_signal_room.py --source-root . --output dist/signal-room-after
python scripts/compare_hollow_trail_signal_room.py --before dist/signal-room-before --after dist/signal-room-after --output docs/images/hollow-trail-signal-room
```

The baseline checkout is pinned to the before commit above. The script compiles
`Apps/hollow_trail.c` directly with a host C compiler, supplies no active external
APIs, renders each selected state and invokes the ordinary mono packer. It needs
Pillow for lossless PNG conversion. Frames preserve the exact raw pixels in the
PNG. Comparison canvases simply paste before and after originals into two columns,
with labels above, grayscale at Y=26 and mono at Y=586.
