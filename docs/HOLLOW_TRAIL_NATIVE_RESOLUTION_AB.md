# Hollow Trail native-resolution A/B mode

Hollow Trail 1.1.31 adds **NATIVE 960X540** to the existing pause-menu graphics comparison. Pause the game and press **B / Up** to cycle through the graphics modes. The production baseline remains the default.

## What changes

The baseline keeps the current performance-oriented pipeline: gameplay/world geometry is rasterized at 480×270, the cached scenery path begins below that resolution, and the final monochrome pack expands the logical image to the 960×540 panel.

NATIVE 960X540 allocates two 960×540 grayscale surfaces in PSRAM. Procedural terrain, character geometry and animation, trees, ropes, props, weather, foreground occluders, puzzle/evidence geometry, camera rotation/zoom, vignette, prompts and other overlays are rasterized into that physical-resolution surface. The monochrome packer then consumes one grayscale sample for each physical e-paper pixel; it does not perform the baseline 2× logical-to-panel interpolation.

The deliberately soft composed backdrop planes still use their existing cached 480×270 composition source and are bilinearly bridged into the native surface before native foreground/world drawing. This keeps the experiment isolated from the cache architecture while removing the coarse 2× raster from the sharp silhouettes and interactive scene geometry that most visibly pixelate.

## Cost and purpose

The mode adds two 960×540 8-bit PSRAM surfaces, about 1,036,800 bytes. It is expected to cost substantial render time and memory bandwidth. It is an A/B quality measurement mode, not a claim that native rendering is faster.

Physics, collision, authored camera values, world coordinates, pacing and controls are unchanged. Switching the mode resets the existing performance window so FPS, render, pack and scan timings can be compared against the same scene.
