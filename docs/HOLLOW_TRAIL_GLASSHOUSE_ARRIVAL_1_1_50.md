# Gallery to the three glasshouse rows

This local continuation starts at `1cd1b1114074c180ade80f3b8a612bfeab3ec1a2`
and keeps the cumulative unreleased **1.1.50** update and **1.3.37** firmware
floor. It connects the last gallery scenery in Chapter VI to the opening
landscape in Chapter VII. It is not whole-book completion.

The existing earned hoist ending and the live first garden support now share
one gallery wall, ground material, reflected water thread and damp fern mouth.
The uneven quarry opening recedes to the left as ordinary player input takes
the traveller into the first room. Chisel marks catch its light. The rear wall
and overhead rock separate at the mouth, revealing the living bed's actual
ribs. The handoff draws that same bed without moving the frozen destination.
This increment adds no cutscene, automatic traversal, interaction, gate or collectible.

Three distant rows replace the four repeated conservatory domes. Their brick
feet sample the existing surveyed ridge; each lower row loses panes until the
last is a bare frame over open planting. The nearer surviving pane has clips,
a sky reflection, inverted seedlings and a small sleeve reflection that moves
only while the traveller is beneath it. Limewash, damp brick feet and an
ankle-height pipe with felt-wrapped joints follow support zero. Thorned growth
frames the mouth, and cut ivy retains dead leaves on the bench leg beyond the
crate. The crate's contact and silhouette remain exposed.

All gallery floor marks use `ht_surface_at` on support zero. The far rows are
background scenery rooted in `ht_chapter_ridge`; they add no walking surfaces.
The existing game geometry, controls, puzzles, evidence, reader, ending logic,
boat/dam rendering, garden care, food, privacy and sister memories are retained.
The application state remains byte-identical through every rendering call.

## Evidence and checks

`scripts/preview_hollow_trail_glasshouse.py` operates the original quarry
balance route and mapped input to earn the hoist. It captures the held handoff,
release, mouth, surviving pane, three rows and a real walk back through the
gallery. Live shots use the production camera. The labelled three-row overview
alone uses a wider render snapshot, with the live state unmodified. Before and
after frames use the same fixture and camera policy. Both original 960×540
grayscale and production packed-monochrome rasters are preserved; comparison
panels are checked byte-for-byte and carry source hashes.

The focused sanitizer test crosses and returns over the original first support,
checks pure/deterministic scenery in both raster paths at close/wide framing,
checks chapter scoping and bounded renderer servicing, and completes the
original garden route, crate and mirrors without deaths. Existing hoist tests
cover exact destination preservation and HID/XInput pause, journal, controller
loss/recovery, cancel, neutral rearming and host exit. Neighboring garden tests,
production renderer references, C++ math and the S3 app/package builder are
recorded in the bounded local evidence archive. Full native aggregate validation
belongs to the integration checkpoint and is not claimed for this increment.
No device appearance, FPS, hardware qualification, release or publication is
claimed here.

## Local validation result

The focused arrival and hoist ASan/UBSan checks pass, including the late gallery
frames in both raster paths. The living-bed, food, evening-care, privacy and
sister-memory sanitizer checks pass. Leak scanning is disabled for this host;
address and undefined-behavior sanitizers remain enabled. All thirty renderer
reference pairs pass: the three reviewed Chapter VII pairs change and the other
27 remain byte-for-byte identical. Strict C++ math/render integration passes.

The cached Xtensa compiler builds the S3 app. Structural ELF and ordinary ZIP
checks pass. Source, sidecar, package manifest and both catalogs agree on
**1.1.50**, with firmware floor **1.3.37**. The version guard compares against
merged `c765a9931e2f` (**1.1.49 → 1.1.50**); this is a continuation of that one
unreleased increment.

- ELF: 447872 bytes; SHA-256
  `1dec29345c50b0c8d57240279d5aaa31575f814ce14a9af33668aee2f1336279`.
- Ordinary ZIP: 449095 bytes; SHA-256
  `3bbd27234085a43d09ed399f5cd9ebb2da209ed39563ec3a2d74cea19b7be1c3`.

The local archive contains the bounded source delta, before/after app source
snapshots, six original grayscale and packed-mono comparisons, reproduction
script, source/raster metadata and validation logs. It excludes historical
image archives and device actions. No remote writes or blocked-publication
retries were made.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
The complete source-labelled scene captures, focused tests and package are
preserved in the delivered evidence archive. GitHub image publication is
still in progress and may be partial; the PR records its exact status.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
