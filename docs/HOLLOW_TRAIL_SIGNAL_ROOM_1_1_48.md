# Hollow Trail 1.1.48: the empty signal room

This is a connected Chapter II increment in the ongoing whole-book adaptation.
The novella and its 44-set visual direction remain the sources. The existing
[scene-by-scene gap map](HOLLOW_TRAIL_NOVELLA_ALIGNMENT.md#scene-by-scene-remaining-map)
remains open; this increment does not complete the book or even every set-11 beat.

## Source and play

The novella's signal-room passage places a rush chair facing the window, an empty
coat hook, a lamp beside a toothed wheel, a lifting metal tongue, a hanging oil
bead and a watch log. The signal continues without an attendant. The protagonist
compares two facing pages at the window; matching signature blots provide a
physical observation, without deciding whose account is true.

- The room is on the existing city upper roof (parcel 7, floor Y=-100), reached
  by the existing ladder at X=2060. Its window remains at X=2098, the same light
  seen from the rain tank and the schoolroom route. No new invisible footholds.
- A naturally grounded crossing at X=2090 earns a one-session arrival tableau.
  It opens slowly, watches two full 144-tick wheel/signal cycles, and returns to
  the exact player, camera, puzzle and evidence state. It never climbs for the
  player, grants a document, solves the relay or runs on a chapter debug jump.
- The visible log moves from the generic rooftop chest at X=2062 to the floor
  beside the chair at X=2170. Its evidence identity remains unchanged. The log prose now uses the novella
  passage directly, removing inherited extra claims about where copies were made
  and dates beyond her departure; the heater-return clue comes from the later
  relay paragraph. A inspects it; left/right compares the seated mechanical
  view, separated pages and physically aligned pages. A/Start reads the
  existing log; X/Back returns. Repeated inspection remains available.
- During comparison only the local mechanism clock runs. Player state,
  traversal, weather state, camera and relay remain frozen. Input edges and a
  neutral-input gate prevent entry/return buttons leaking into gameplay.
- The existing city narration about the unattended lamp now begins at this
  room, rather than several roofs after it. Reaching X=2065 on the lower roof
  cannot reveal it; the upper floor must be reached. Its text is unchanged.
- The room uses the actual roof as weather cover and 0.68× original nominal
  walking speed while grounded inside. Jump timing, ladder contact, Y motion
  emphasis and the existing city route remain intact.

## Actual screenshots

See [all before/after captures](images/hollow-trail-signal-room/README.md).
The same host harness captures baseline `1d97f4f4` / 1.1.47 and this 1.1.48 source.
Full frames are 960×540 grayscale and actual production-packed monochrome.
Baseline log inspection uses the app's real compact-lettering journal fallback,
with no host typography provider. The new views are real game pixels, not
concept art. Device contrast, ghosting and FPS are not inferred from them.

## Remaining Chapter II and whole-book work

- The brief submerged-street glimpse before the fuse crossing is still missing.
- Set 11 still needs the full westward warehouse/siding/pump-country transition
  and the explicit lower-window lighting response to the player-operated relay.
- Earlier set-4/5/6/7 details and mill desk movement remain on the gap map.
- Later chapters still have their existing playable traversal and puzzles, but
  many narrative spaces/interactions remain partial. Set 44's dedicated final
  door and static waiting end are still absent; the old ten-chapter loop does
  not demonstrate completion of that ending.

## Version and verification

Hollow Trail **1.1.47 → 1.1.48**. Current master and the live release index both
used 1.1.47 before this increment. Minimum firmware stays 1.3.37. The app's
startup/pause version labels now match the manifest. No firmware changes.

Local verification completed: the full native-app aggregate passed; after the
last review changes, the signal-room/control ASan+UBSan checks and full route were
rerun successfully. Leak detection was disabled locally because the runner uses
ptrace; CI retains its own normal settings. All 30 existing scene/mono golden
pairs and 300 grotto references remained exact. Eleven before/after views include
both grayscale and packed mono, and every comparison canvas was checked against
its full-size originals. No new allocations were added to the app.

The final Xtensa app build, ELF structural validation, ordinary package ZIP
validation, version guard and manifest/sidecar/package/catalog agreement passed.
Final ELF SHA-256: `6352c13942a1be28b42ad0f8b0f2c25fee325feb5766acced4d352a6775cdcff`.
Exact-head hosted CI is recorded in the PR and must not be inferred from local
checks. New checks
cover both rasters, deterministic complete frame writes, bounded render service,
physical evidence position, comparison controls, animated clock with frozen
play state, one-shot earned arrival, two complete cycles, and unchanged handoff.
Existing route/terrain, intro/schoolroom/rain, renderer and boat references are
retained. The app-specific Xtensa builder checks ELF structure, ordinary ZIP
export/validation and source/sidecar/package/catalog version agreement.

No merge, release, installation or device flash is part of this increment.
