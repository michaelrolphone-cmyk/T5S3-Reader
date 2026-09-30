# Hollow Trail composed environments and production renderer

## Rendering baseline

Version 1.1.22 includes the device-tested nearest-camera experiment from 1.1.21
on top of mode 24 + mode 13. The owner reported that only nearest camera
improved FPS in the last experiment set. No numerical gain is assumed.
Alternate-background retention, motion-resolution sampling and their selector
are removed. Scenery updates every frame. Camera transforms use a rounded
nearest source sample; AI reconstruction, SIMD and dithering remain available.

## Composed environments

Every chapter has an authored ridge profile, three independently moving depth
planes, a chapter-specific light field and terrain contact highlights. Existing
chapter architecture and mechanics remain in place. Three distant openings
carry localized light across the chapter without switching light abruptly.
The established boat grotto retains its dedicated lighting/water renderer.
Its later shore now includes climbable living trees.

The first forest receives the concentrated visual work: a dedicated full-scene
atmospheric background, rolling hills, monumental parallax groves, shafts of
light between silhouettes, five massive foreground trees with buried buttress
roots, lit bark facets, organic canopy masses and walkable limbs. Roots sample
the actual soil. Branches use the same sloping height for art and player support.
The intent is visual quality; this adds per-frame work and is not an FPS claim.

## Tree traversal

The forest has five living trees; the marsh has two, the garden three, and the
mountain chapter three. Each has five standing branches (65 total).

- Approach either trunk edge and press A to grip, or Up/Down to start climbing.
- Hold Up/Down to ascend/descend. Hands and feet animate against the trunk.
- Press outward at a branch to step onto it. Walk along its sloping surface.
- B jumps off; A releases a grip. A short cooldown prevents an immediate regrab.
- Down on a branch drops through it; branches also allow upward jumps through.
- Falling onto a branch catches the character. Branches shelter precipitation.

Trunks sit immediately behind the ground route and can be walked past. These
optional canopy routes do not replace the original puzzle/evidence path. Only
the authored living-tree plane is climbable; distant silhouettes are scenery.

## Validation

The native tests cover every tree/branch: grabbing, ascent, stepping out, slope
walking, dropping, landing, upward passage and jump release. The input-only
route witness still completes all ten chapters, puzzles and evidence without
respawn, and deliberately drops from optional branches when following ground
objectives. Raster regression captures include all chapters and readiness
masks; independent camera sampling, grotto shading, render-snapshot servicing,
controls, display ownership and allocation cleanup remain covered.

Visual review uses actual software-rendered forest views at ground and canopy
height and a ten-chapter contact sheet. The ESP32-S3 build validates the actual
ELF. Source, sidecar and catalog version/digest agreement must be checked.
Device appearance and FPS for these richer environments are not yet measured.
