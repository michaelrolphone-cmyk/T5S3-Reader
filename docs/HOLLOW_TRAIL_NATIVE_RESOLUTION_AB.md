# Hollow Trail native-resolution A/B mode

Hollow Trail 1.1.31 adds **NATIVE 960X540** to the existing pause-menu graphics comparison. Pause the game and press **B / Up** to cycle through the graphics modes. The production baseline remains the default.

## What changes

The baseline keeps the current performance-oriented pipeline: gameplay/world geometry is rasterized at 480×270, several scenery passes are evaluated below that resolution, and the final monochrome pack expands the logical image to the 960×540 panel.

NATIVE 960X540 allocates two 960×540 grayscale surfaces in PSRAM. Forest, city and authored chapter backdrops are evaluated across the physical raster instead of being expanded from their 2×2 logical shading blocks. The grotto's rock, water, reflection and light composition is also evaluated at physical resolution. Procedural terrain, character geometry and animation, trees, ropes, props, weather, foreground occluders, puzzle/evidence geometry, camera rotation/zoom, vignette, prompts and other overlays are rasterized into the same physical-resolution surface.

The monochrome packer consumes one grayscale sample for each physical e-paper pixel and does not perform the baseline 480×270 → 960×540 interpolation. The only retained lower-resolution source is the pre-existing AI/cache depth reconstruction used beyond the grotto in the boat chapter; that distant cached source is bilinearly seeded into the native surface before the native traversal/foreground pass. The dedicated grotto itself is native.

## Cost and purpose

The mode adds two 960×540 8-bit PSRAM surfaces, about 1,036,800 bytes. It deliberately bypasses the composed-backdrop caches for native forest/city/authored chapters, so it is expected to cost substantial render time and memory bandwidth. It is an A/B quality measurement mode, not a claim that native rendering is faster.

Physics, collision, authored camera values, world coordinates, pacing and controls are unchanged. Switching the mode resets the existing performance window so FPS, render, pack and scan timings can be compared against the same scene.
