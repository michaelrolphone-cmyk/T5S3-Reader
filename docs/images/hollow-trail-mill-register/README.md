# Mill register: actual before and after

Before is the Hollow Trail 1.1.48 app source at
`b18a5abf0565b14ea67169338ce4636425e5b386`, merged unchanged into master
`b71a420788cfdbb758fb5bb0eada0e82fc497122`. After is this 1.1.49 mill candidate.
[Metadata](comparison-metadata.json) records exact app-source SHA-256 values,
scene fixtures, raster hashes and comparison counts for both sides.

- [Approach: the desk begins in shade](shadow-before-after.png)
- [Desk drawn into the window beam](drawn-before-after.png)
- [Arrival: the register is not revealed prematurely](arrival-before-after.png)

Each comparison keeps original 960×540 grayscale above and production-packed
monochrome below. Pixel regions were checked byte-for-byte against their source
frames; no scaling or generated illustration is used. Individual originals remain
available by reproducing the script. These are host game rasters, not e-ink photos.

The drawn after-state uses actual A-grip, Right movement and inspection calls;
it does not teleport the body into place. The old source has a fixed register and
therefore uses its existing inspection at that same reached player position.
Both sides use the same camera/framing. Arrival uses the existing mill tableau
at tick 170. The camera fixture and input path are in the metadata and script.

```sh
python scripts/preview_hollow_trail_mill.py --source-root ../hollow-mill-before --output dist/mill-before
python scripts/preview_hollow_trail_mill.py --output dist/mill-after --compare-before dist/mill-before
```

Use a before checkout pinned to the source commit above, a host C compiler and
Pillow. The same script is used on both checkouts and invokes the ordinary game
renderer and mono packer. After light inspection, the original journal is still
available through the ordinary controls.
