# Hollow Trail native-resolution A/B mode

Hollow Trail 1.1.31 added **NATIVE 960X540** to the existing pause-menu graphics comparison. Hollow Trail 1.1.33 changes that mode to a hybrid-resolution comparison: the distant authored backdrop is rendered at 480×270 while the foreground remains native 960×540. Pause the game and press **B / Up** to cycle through the graphics modes. The production baseline remains the default.

## What changes

The baseline keeps the current performance-oriented pipeline: gameplay/world geometry is rasterized at 480×270, several scenery passes are evaluated below that resolution, and the final monochrome pack expands the logical image to the 960×540 panel.

NATIVE 960X540 allocates two 960×540 grayscale surfaces in PSRAM. In 1.1.33, the forest, city and authored-chapter distant backdrops are evaluated at 480×270 and expanded once into the physical surface. The expensive near scene stays native: procedural terrain, character geometry and animation, trees, ropes, props, weather, foreground occluders, puzzle/evidence geometry, camera rotation/zoom, vignette, prompts and other overlays are rasterized at 960×540. The boat grotto remains on its dedicated native path rather than being forced through the authored-backdrop reduction.

The monochrome packer consumes one grayscale sample for each physical e-paper pixel and does not perform the baseline 480×270 → 960×540 interpolation. The only retained lower-resolution source is the pre-existing AI/cache depth reconstruction used beyond the grotto in the boat chapter; that distant cached source is bilinearly seeded into the native surface before the native traversal/foreground pass. The dedicated grotto itself is native.

## Cost and purpose

The mode still adds two 960×540 8-bit PSRAM surfaces, about 1,036,800 bytes, because the foreground and camera pipeline remain physical-resolution. The 1.1.33 hybrid reduces distant-background raster work by four physical samples per logical backdrop sample while preserving native-resolution silhouettes and interactive geometry. It remains an A/B quality/performance measurement mode; device FPS must establish the actual gain.

Physics, collision, authored camera values, world coordinates, pacing and controls are unchanged. Switching the mode resets the existing performance window so FPS, render, pack and scan timings can be compared against the same scene.
