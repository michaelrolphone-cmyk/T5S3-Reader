# Hollow Trail: the westward city view

This is the Chapter II view after the signal room, from the novella's paragraph
beginning “When the rain weakened.” It continues the ongoing whole-book adaptation.

After physically reaching and inspecting the watch log, walking across the right
edge of the signal room's existing upper roof earns the view once per session.
The lower tank roof, an airborne crossing, a debug jump or a player who has not
inspected the log cannot trigger it. It does not solve the relay or alter its
windows, evidence, traversal or chapter state.

A 640-tick timeline (about 20 seconds at the existing 32 ms step) dissolves from
the reached roof into five depth planes: low cloud and lit bluffs, flat country
with stopped bent-neck pumps, fenced railway sidings and a drainage cut, stepped
warehouses and a flooded street beneath rooms, and a close cracked cornice with
grass. The near layers drift farther than the distant horizon. Masonry, roof
seams, window damage and worn ballast are fixed to their objects. The scene uses
the existing native/low raster primitives and production monochrome packer.

The final dissolve returns the exact reached roof raster, including its actual
camera sway and framing. Gameplay stays frozen throughout; neutral input is
required before walking resumes. Device exit remains available. Rendering uses
existing scratch arenas after the ordinary camera has finished with them and
introduces no per-frame allocation.

The capture script `scripts/preview_hollow_trail_western.py` records actual
production frames and source hashes. The before side is the ordinary reached
roof raster because this scenic view did not exist there. The after side is the
real earned timeline. Departure and return controls match the before raster
exactly; all comparison regions retain their original full-size pixels.

Focused ASan/UBSan checks cover both rasters, complete deterministic writes,
bounded service checkpoints, visible parallax after mono packing, real
HID/XInput walking into the trigger, held controls, exact state/frame return,
neutral-input handoff, one-shot behavior and host exit. Final combined aggregate,
app build and exact-head CI outcomes belong in the owning PR.

This cumulative unreleased increment retains Hollow Trail 1.1.49 above the
1.1.48 base; recheck the current published lineage before integration. The
explicit signal-room handprint/window entry, earlier submerged-street glimpse,
and the rest of the 44-set map remain open. No device appearance, ghosting or FPS
qualification is claimed.
