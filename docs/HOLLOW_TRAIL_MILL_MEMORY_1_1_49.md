# Hollow Trail 1.1.49: the register remembers

This is another coherent slice of the ongoing whole-book adaptation. It follows
the desk-into-light action in the same cumulative unreleased **1.1.48 → 1.1.49**
update; the firmware minimum remains 1.3.37. It does not complete the entire
novella or authorize a merge, release or physical device qualification.

## Earned scene and visible actions

The production A/Confirm path can begin the memory only after actual grounded
inspection of the register on the moved, grounded desk inside the window beam.
The existing evidence ID remains 1. A dark desk, an airborne desk or traveler,
an unreached register and an archive entry elsewhere cannot trigger the scene.

The fixed 32 ms cutscene clock gives seven cues **46.08 seconds** in total:

1. The register fills the view with its scraped heading, seven ruled names,
   squares, differing hands and the damaged word *admission*.
2. The memory inhabits a sloping desk in hard window light. The sister's
   shoulders are unequal; she holds the pen low and moves the back of her other
   wrist to her ear as the loose hair shifts behind it.
3. At the departure doorway she makes room for a second loaf in her bag. The
   first loaf remains visible, and the bag lip occludes the new loaf as it goes
   in. The half-wound scarf hangs below her coat.
4. She tries the weighted shoulder strap while the book's invitation is read.
5. Back in the mill, the sheet trembles against the thumb; the other hand
   crosses the six lower names and steadies it. Her first row stays exposed.
6. That hand moves away, uncovering the names again.
7. The final written line and signature are legible on the paper:
   *I kept the last square open.* / *the keeper*.

All narration comes from the supplied novella. The 2,074-byte archive entry
contains the full mill passage from broken-shutter entry through *There was no
name*, with the existing ASCII typography normalization and original paragraph
breaks. Unspecified names are drawn as handwriting, not invented as readable
identities. The journal no longer substitutes speculative register prose.

## State, repetition and camera

The memory uses the existing articulated-joint/raster system and cutscene
timeline. It allocates no new image arena, texture or per-frame heap memory.
Raster work uses the existing input/service checkpoints. The moved desk,
persistent scrape, evidence, chapter puzzle and entire gameplay state remain
frozen. Natural completion opens the register in the existing journal and
requires neutral input. Held A cannot skip or advance the following page.

X or Start opens that page early; device Back/Home and host exit retain their
existing exit meaning. Journal Back goes to the index and then the trail.
Physical reinspection deliberately replays the memory, while reading the
already-earned archive page does not. Replay never repeats a reward or resets
the desk. Both HID and XInput exercise these flows.

The live interior camera eases outdoor rotation and breathing to zero near the
mill floor, with a 40-world-pixel transition at each wall. It retains authored
intimacy, input, simulation, stored camera phase and ordinary outdoor/chapter
behavior. The memory itself uses held framing so the small actions can read.

## Evidence and verification

[Original before/after captures](images/hollow-trail-mill-memory/README.md) use
the exact same script and actual device input on both source snapshots. The
baseline opens its original journal; the changed source plays the memory.
Images are real production 960×540 raster and packed monochrome, not paintings
or reconstructed reference art. Raw frame hashes and all source-file hashes
are recorded, and comparison crops are checked byte-for-byte.

Focused ASan/UBSan checks cover physical eligibility, complete state retention,
hand/hair/loaf motion, page trembling, per-row six-name occlusion and first-name
preservation, both raster modes, prior-frame independence, normal completion,
X/Start, repeat/archive/exit and quiet interior camera bounds. Exact novella
prose comparison, existing mill/cutscene/direction checks, 1,360 caption-safe
camera cases and the full ten-chapter route also pass. All existing production
renderer golden pairs remain unchanged, including the boat scenes. A focused
regression reproduces inadequate coverage of the first lower name on 459b821d
(25/172 ink pixels). Raising the same complete hand seven logical pixels covers
69/172 ink pixels there (144/367 in native raster), and covers at least 39% on
each lower row while the sister's first-name strokes stay byte-identical.

The S3 app builder, ELF structure, ordinary ZIP export and offline package
validation pass. Source, sidecar, compatibility catalog and bundle catalog
retain 1.1.49. The parent integration records the final combined artifact and
hosted CI results. Local sanitizer execution disables leak scanning for the
runner's ptrace limitation; no physical FPS/ghosting claim is made.

## Remaining work

Explicit broken-shutter entry remains on the active 44-set gap map. This scene
does not add a new mill entrance collision path or replace the continuous
forest/rope route. Other incomplete early-forest, city and later chapter scenes
remain separate work; the dedicated ending door is still pending.
