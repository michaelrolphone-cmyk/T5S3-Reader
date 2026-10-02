# Hollow Trail production renderer

Version 1.1.22 makes the device-tested nearest-camera experiment from 1.1.21
part of the default renderer, on top of the established mode 24 + mode 13 base.
The owner reported that only nearest camera improved FPS in the latest tests.
No numerical FPS gain is assumed.

Camera rotation and breathing now use one rounded nearest-neighbor source
sample per visible pixel instead of four bilinear taps. Camera movement,
framing, physics, AI background reconstruction, SIMD stages and dithering are
unchanged. Nearest sampling can produce sharper edges and visible pixel steps.
The transform is still skipped when the camera effect is inactive.

The unsuccessful alternate-background and motion-resolution experiments,
combined mode, pause-menu selector and optional 129,600-byte retained scenery
buffer are removed. Scenery updates every rendered frame at the existing
resolution. No mode selection is needed. The rolling ten-second FPS display
and pause/resume measurement reset remain available.

Validation uses golden frames captured from the 1.1.21 nearest-camera mode
across all chapters and scalar/SIMD readiness masks, an independent rounded
coordinate oracle, and grotto reference coverage. The native regression suite
also covers controls, pipeline ownership/fallback/cleanup and cooperative
render servicing. The ESP32-S3 build checks the actual distributable ELF;
source, sidecar and catalog versions and the ELF digest must agree.

## 1.1.23: restore the landscape; climb its existing trees

The owner rejected the 1.1.22 scenery replacement: unnatural branch spacing,
visually worse landscapes and roughly one-third of the earlier device FPS.
This correction removes that replacement, including its full-frame composed
backdrop/contact passes and the separately authored living-tree arrays.
Nearest-camera sampling and the earlier production optimizations remain.

The original world-tree placement, heights, bends, seeded limbs, ground route,
roots, grotto and chapter scenery are restored. Existing substantial limbs now
provide footing using the same geometry that draws them, including the forest's
rope-anchor limb. Small twigs, background parallax trees and the thin outer limb
tips are not traversable. No trees, branches or platform connections are added.

Tree entry requires Up at a trunk edge, with neither horizontal direction nor
held jump. A never grabs a tree. Climbing follows the original bent, tapered
trunk. Horizontal input always releases; at a limb it steps onto its surface,
otherwise the character falls. B jumps away, Down descends while climbing or
drops through a standing limb. Release cooldown prevents immediate recapture.
Tree/branch instruction overlays are removed. Input mappings include generic
keyboard arrows and the existing HID/XInput controller paths.

The first forest receives a restrained lighting adjustment within its existing
cached layers: two world-anchored openings, light settling on the lower canopy,
and a clearer tonal separation between distant and middle trees. Shape, tree
spacing and terrain do not change. This light is baked only on strip generation,
with cooperative checkpoints; there is no new full-screen lighting pass per
frame. Other chapters return to the pre-overhaul nearest-camera reference frames.

### Host evidence (not device FPS)

An `-Os` host comparison used four fixed forest views and four grotto views,
200 warm renders per view, camera effects active. Scalar host paths, no display
or input servicing. The cold column includes the initial render at each view.

| Revision | Forest cold ms | Forest warm ms | Grotto warm ms |
| --- | ---: | ---: | ---: |
| Rejected merged 1.1.22 (`59f63f8f`) | 3.619 | 1.636 | 0.452 |
| Original nearest-camera scenery (`1e895c47`) | 2.329 | 0.360 | 0.431 |
| Corrected scenery and cached light | 2.670 | 0.382 | 0.441 |

These short host measurements identify the removed cost; they do not predict
ESP32-S3 FPS or guarantee recovery of the owner's earlier device measurements.
The correction's visual quality and device FPS are not hardware-qualified.

Validation includes original nonforest frame/packed-output references,
reviewed updated forest captures, all readiness masks, cache seams/reuse,
Up-only entry, diagonal/sideways jumps without tree attachment, A inspection,
branch landing/walking/drop, climb-to-branch exits, and the complete chapter
route. `scripts/build_all_apps.py --id hollow_trail` checks the S3 ELF and writes
matching version/digest metadata. App identity advances 1.1.22 -> 1.1.23;
minimum firmware stays 1.3.37.
