# Hollow Trail — Playable Scene Set Direction

This document converts the novella's visual sequence into **44 reusable playable scene sets**. A playable set may contain several narrative cuts when the same geometry and asset family can be reused with different framing, weather, foreground occlusion, lighting, or staging.

The direction is intended to preserve the story's environmental arc: **domestic stillness → organic forest → vertical city → exposed industrial country → quiet water → monumental quarry and dam → emptied high country → dense settlement → absolute stillness at the final door.**

## Intro cutscene and gameplay handoff

The first cinematic should **linger**. Its current authored duration is about **61 seconds**, not a rapid montage. Text-bearing shots remain on screen for roughly five to seven seconds, and the two tall-grass memory shots each remain for more than seven seconds. The player should have time to read the two lines, stop reading, and still have several seconds left to study the environment and notice small motion.

The opening still uses **Forward-X** for the kitchen and memory: X is depth, Y is screen-left/right and Z is vertical. Those tableaus must read as finished scenes rather than visualization wireframes. Use filled perspective furniture/architecture, solid character silhouettes, substantial tree anatomy, irregular crown masses, surface wear, condensation, floor seams and botanical detail. Thin limbs are reserved for genuinely thin things such as grass stems, twigs, braids and small metal parts.

After the return to the kitchen and packing, continuity switches directly to the normal **profile** grammar at the house exterior. The protagonist is first seen inside a dark open doorway and walks out through the door. The camera then accompanies her continuously through the garden wall, orchard, ditch and into the road where roots and much larger forest trunks gradually replace domestic scenery. The forest approach must be world-rendered before it reaches the viewport; no tree group or depth layer may be gated on the protagonist crossing an on-screen trigger.

The final cue still converges on the live Chapter I spawn's x-position, camera placement and 1.5x intimacy scale before input is released, with neutral gating preventing held buttons from leaking through the handoff.

## Effect scale

| Control | 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- |
| Camera sway | off | ±0.5° / 40 s | ±1.2° / 34 s | ±2.3° / 28 s | ±3.8° / 22 s | ±5.8° / 18 s |
| Breathing zoom | 1.000× | 1.00–1.015× | 1.00–1.03× | 1.00–1.055× | 1.00–1.09× | 1.00–1.15× |
| Weather | none | barely perceptible | light | definite/local | heavy | dominant |
| Foreground occlusion | none | 5–10% | 10–20% | 20–30% | 30–40% | 40–55% |
| Depth / parallax | 2 planes | 3 planes | 4 planes | 5 planes | 6 planes | 7+ planes |

### Global camera rules

- Camera motion must remain much slower than character animation.
- During precision jumping, ropes, narrow ledges, ladders, boat landings, and other failure-sensitive movement, temporarily halve camera rotation regardless of the scene preset.
- Large vistas use authored one-time pullbacks. Do not simulate scale merely by increasing continuous sway or breathing zoom.
- Foreground observer scenery should be sparse, large, and physically plausible: trunks, masonry corners, rock faces, reeds, greenhouse frames, buttresses, or walls that occasionally obstruct the observer's view of the protagonist.
- Close scenery remains sharp. Diffusion and loss of contrast increase with depth rather than softening the entire scene.
- Environmental movement should come from the environment whenever possible: branch flex, water, leaves, rain, pipes, machinery, cloth, boat motion, or light. Do not replace these with generic camera shake.
- Natural walking is the baseline. Running is reserved for explicitly urgent moments and must use the running animation.

---

## I. The Backward Stitch

The opening moves from intensely domestic space into increasingly large forest compositions, culminating in the first city reveal.

### 1. Kitchen Window

- **Movement speed:** 0.65×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 2 — dense fog beyond the glass
- **Foreground occlusion:** 0
- **Depth:** 2 planes
- **Direction:** Almost static. Fog gradually consumes exterior layers instead of visibly racing across the scene. Keep the protagonist large in frame. The three flashes should be the brightest visual event.
- **Beat:** On the final flash, use an almost imperceptible 1.015× → 1.03× push toward the glass.

### 2. Tall-Grass Memory

- **Movement speed:** 0.80×
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1 — warm wind through grass
- **Foreground occlusion:** 4
- **Depth:** 4 planes
- **Direction:** Tall grass repeatedly crosses the observer's view. The flattened hiding place is intimate and enclosed without becoming claustrophobic.
- **Beat:** Mirror flashes make brief local exposure changes, never a full-screen flash.

### 3. Pre-dawn Departure / Orchard

- **Movement speed:** 0.75×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 1 — low mist
- **Foreground occlusion:** 1
- **Depth:** 4 planes
- **Direction:** Begin close behind the character and gradually open the frame as the house, orchard, and chimney disappear.
- **Beat:** Ease toward roughly 0.92× framing by the final view of home. No running.

### 4. Deep Forest / Marked Tree

- **Movement speed:** 0.78×
- **Camera sway:** 3
- **Breathing zoom:** 3
- **Weather:** 2 — fog, dripping water, intermittent leaf fall
- **Foreground occlusion:** 4
- **Depth:** 5 planes
- **Direction:** Giant near trunks repeatedly block 25–35% of the scene. Distant hills move very little; intermediate trunks provide most parallax.
- **Beat:** At the cloth marker, suppress sway to level 1 and push to about 1.08× for inspection.

### 5. Fallen-Tree Hollow

- **Movement speed:** 0.72× exploration; 0.55× during pushing
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1 — dripping canopy
- **Foreground occlusion:** 3
- **Depth:** 4 planes
- **Direction:** The tree should visibly resist the protagonist before moving.
- **Beat:** Require roughly 2–3 seconds of visible effort and partial movement, then let the trunk begin falling slowly and accelerate under gravity. Hold framing until motion starts, then pull back about 8%.

### 6. Mill Hollow

- **Movement speed:** 0.72×
- **Camera sway:** 2 exterior / 0 interior
- **Breathing zoom:** 2 exterior / 1 interior
- **Weather:** 2 — drizzle and wet vegetation
- **Foreground occlusion:** 3 exterior / 0 interior
- **Depth:** 5 exterior / 2 interior
- **Direction:** Exterior should be dense, wet, and layered. Interior should become geometrically still with hard window light, dust, desk, and register.
- **Beat:** Evidence discovery calms the camera instead of dramatizing it.

### 7. Ravine → Clearing → Gate → City Reveal

- **Movement speed:** 0.70× crossing; 0.82× clearing
- **Camera sway:** 1 rope / 3 clearing
- **Breathing zoom:** 1 rope / 3 clearing
- **Weather:** 1
- **Foreground occlusion:** 2 → 0
- **Depth:** 5 planes
- **Direction:** The living branch bends visibly under load. The clearing should abruptly feel wider than the forest.
- **Beat:** Entering the clearing triggers about a 0.90× vista pullback. Opening the gate pulls farther to roughly 0.84–0.87× and holds layered towers in the fog for several seconds before normal follow resumes.

---

## II. Rooms Above the Fog

The city replaces horizontal forest depth with vertical distance, precarious crossings, intermittent rain, and unreliable artificial light.

### 8. Service Terrace / First Rooftops

- **Movement speed:** 0.82×
- **Camera sway:** 3
- **Breathing zoom:** 3
- **Weather:** 2 — mist
- **Foreground occlusion:** 2 — masonry corners
- **Depth:** 5 planes
- **Direction:** Lower floors disappear into white fog. Nearby walls frequently interrupt the view. Rooftop parallax should be strong while distant towers barely move.

### 9. Schoolroom / Rooftop Route

- **Movement speed:** 0.70× interior; 0.85× exterior
- **Camera sway:** 0 interior → 2 exterior
- **Breathing zoom:** 1 interior → 2 exterior
- **Weather:** 2
- **Foreground occlusion:** 0 interior → 2 exterior
- **Depth:** 2 interior → 5 exterior
- **Direction:** Interior is contemplative and still, dominated by desks, map, and window grid. Leaving restores lateral movement and rooftop depth.

### 10. High Crossings / Rain Tank

- **Movement speed:** 0.82× normal; 0.70× precision sections
- **Camera sway:** 2 normal / 1 on ledges
- **Breathing zoom:** 2
- **Weather:** 4 — localized hard rain
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Rain becomes the dominant moving layer while the buildings remain massive and slow.
- **Beat:** Brief fog openings reveal streets far below, then erase them again. No aggressive rotation during precision traversal.

### 11. Signal Room / Western Relay

- **Movement speed:** 0.68× interior; 0.85× after relay restoration
- **Camera sway:** 0 interior / 2 exterior
- **Breathing zoom:** 1
- **Weather:** 3 → 1 as rain weakens
- **Foreground occlusion:** 1
- **Depth:** 3 interior / 5 exterior
- **Direction:** Automated flashes should initially imply occupancy. Once the mechanism is understood, nearly all environmental motion should stop except the machine.
- **Beat:** Restoring the relay lights a row of windows and widens the western industrial view.

---

## III. The Eighth Place

Oil country should feel exposed, engineered, and suspiciously orderly after the city.

### 12. Pumpjack Plain

- **Movement speed:** 0.86×
- **Camera sway:** 3
- **Breathing zoom:** 2
- **Weather:** 2 — steady crosswind
- **Foreground occlusion:** 1
- **Depth:** 5 planes
- **Direction:** Use wide framing. Pumpjacks remain stopped while grass and wind move around them. Preserve emptiness rather than filling the plain for visual density.

### 13. Tank Basin / Service Cut

- **Movement speed:** 0.82× normal; 0.68× at the culvert
- **Camera sway:** 3
- **Breathing zoom:** 3
- **Weather:** 3 — grit gusts
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Curving tank walls create large foreground wipes. Local gusts briefly obscure distant geometry and then reveal it again.

### 14. Stove Shed

- **Movement speed:** 0.62×
- **Camera sway:** 0
- **Breathing zoom:** 1
- **Weather:** 0 inside; exterior wind remains visible/audible through openings
- **Foreground occlusion:** 0
- **Depth:** 2 planes
- **Direction:** Claustrophobic and still. Knocking is shown physically through pipe vibration, soot fall, and rings in standing water.
- **Rule:** No camera shake. The environment itself reacts.

### 15. Distribution Station / Eighth Line

- **Movement speed:** 0.72× investigation; 0.86× departing
- **Camera sway:** 1
- **Breathing zoom:** 2
- **Weather:** 2
- **Foreground occlusion:** 1
- **Depth:** 4 planes
- **Direction:** Evidence discovery tightens framing slightly. Keep the vessel puzzle visually clean.
- **Beat:** Solving it steadies service lamps into a line leading toward the railway rather than creating a dramatic flash.

---

## IV. The Cancelled Train

Railway scenes emphasize dormant mass, linear perspective, and the strange completeness of machinery that will not move.

### 16. Carriage Night → Rail Yard Dawn

- **Movement speed:** 0.55× night; 0.82× dawn
- **Camera sway:** 0 → 2
- **Breathing zoom:** 1 → 2
- **Weather:** 0
- **Foreground occlusion:** 1
- **Depth:** 2 → 5 planes
- **Direction:** Sleeping carriage is nearly static. Dawn expands into long rails, locomotive, goods shed, and blocked cutting.
- **Beat:** Use a 0.90× reveal pull when the locomotive becomes fully visible.

### 17. Station / HOME Ticket

- **Movement speed:** 0.70×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 1 — light exterior wind
- **Foreground occlusion:** 1
- **Depth:** 3 planes
- **Direction:** Benches and track vanishing points dominate. Let the blocked cutting remain visible through openings. Emotional emphasis comes from stillness rather than zooming.

### 18. Broken Trestle / Signal Cabin

- **Movement speed:** 0.68× trestle; 0.74× cabin
- **Camera sway:** 1 trestle / 0 cabin
- **Breathing zoom:** 1
- **Weather:** 1
- **Foreground occlusion:** 1
- **Depth:** 5 trestle / 3 cabin
- **Direction:** Precision traversal gets almost no sway. The signal cabin opens long views down multiple routes, while puzzle manipulation remains fixed-frame.

### 19. Shunting Yard → Marsh Transition

- **Movement speed:** 0.78× moving wagons; 0.86× exiting
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1
- **Foreground occlusion:** 1
- **Depth:** 5 planes
- **Direction:** Heavy wagons accelerate slowly and retain momentum.
- **Transition:** Over roughly 20–30 seconds of travel, remove hard industrial foregrounds and replace them with reeds, reflective water, and softer depth planes.

---

## V. Water Held Between Banks

The marsh uses reflection, quiet water, and restrained motion.

### 20. Ferry Bank / First Boat Crossing

- **Movement speed:** 0.70× ashore; deliberately slow rowing
- **Camera sway:** 1 ashore; procedural sway disabled afloat
- **Breathing zoom:** 2
- **Weather:** 1
- **Foreground occlusion:** 3 — reeds
- **Depth:** 4 planes
- **Direction:** Initial boat silhouette should plausibly resemble a person. Afloat, hull motion replaces procedural sway, with about ±0.7° ordinary roll.
- **Visual:** Reflections move subtly differently from foreground reeds and hull edges.

### 21. Raised Marsh Path

- **Movement speed:** 0.80×
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1 — breeze
- **Foreground occlusion:** 3
- **Depth:** 5 planes
- **Direction:** Large near willows periodically obscure the protagonist. The distant quarry barely moves. Cloud/light changes can turn visible shallows into mirror-like water.

### 22. Waiting Awning

- **Movement speed:** 0.62×
- **Camera sway:** 0
- **Breathing zoom:** 1
- **Weather:** 1
- **Foreground occlusion:** 1
- **Depth:** 3 planes
- **Direction:** A deliberate rest scene: bench, chair, groove, bell rope.
- **Beat:** Ringing the bell creates one visible environmental response—water, birds, reeds—but no answering person.

### 23. Lock Chambers

- **Movement speed:** 0.72×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 0–1
- **Foreground occlusion:** 1
- **Depth:** 4 planes
- **Direction:** Water level is the primary scene motion. Keep the camera vertically anchored while the masonry walls descend relative to the protagonist.

### 24. Upper Marsh / Quarry Reveal

- **Movement speed:** 0.84×
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Begin with a backward view over the marsh and progressively shift attention forward.
- **Beat:** End around 0.88× framing as the quarry resolves into a large pale wound in the hillside.

---

## VI. The Weight Left Over

The quarry must dwarf the protagonist while keeping surfaces and tool marks sharply described.

### 25. Quarry Floor

- **Movement speed:** 0.80× normal; 0.60× moving stone
- **Camera sway:** 3
- **Breathing zoom:** 3
- **Weather:** 2 — dust
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Huge terraces and crane dominate. Keep the protagonist relatively small.
- **Visual:** Dust travels in broad translucent/dithered sheets instead of particle snow.

### 26. Ladder / Ledge Ascent

- **Movement speed:** 0.70×
- **Camera sway:** 1 climbing; 2 on secure terraces
- **Breathing zoom:** 2
- **Weather:** 2 — gusts
- **Foreground occlusion:** 1
- **Depth:** 5 planes
- **Direction:** Vertigo comes from composition and exposed drop rather than aggressive camera rotation. Human-scale cloth markers and footprints remain visible against monumental stone.

### 27. Hoist / Shaft Descent

- **Movement speed:** 0.62× setup and boarding
- **Camera sway:** 0 inside cage
- **Breathing zoom:** 1
- **Weather:** 1 dust outside / 0 in shaft
- **Foreground occlusion:** 1
- **Depth:** 5 exterior → 3 shaft
- **Direction:** The cage may feel mechanically unstable but the camera stays attached to it. Passing strata, damp seams, roots, and ferns create the descent.

### 28. Lower Gallery / Glasshouse Reveal

- **Movement speed:** 0.78×
- **Camera sway:** 1 → 2
- **Breathing zoom:** 1 → 3
- **Weather:** 0
- **Foreground occlusion:** 1 → 3 foliage
- **Depth:** 3 → 5 planes
- **Direction:** Start dark and constricted. Warmth and vegetation progressively enter the composition.
- **Beat:** The first living glasshouse should open through a slow 1.04× → 0.92× pull rather than a hard cut.

---

## VII. Things Still Growing

This chapter is the visual breathing space. Warmth and life are genuine even while the evidence becomes more disturbing.

### 29. Living Glasshouse Beds

- **Movement speed:** 0.72×
- **Camera sway:** 1
- **Breathing zoom:** 2
- **Weather:** 0
- **Foreground occlusion:** 4 — leaves and frames
- **Depth:** 4 planes
- **Direction:** Dense but quiet. Leaves bend when touched. Water droplets, pipes, seedlings, and broken panes provide micro-motion.
- **Visual:** Highest local detail density reached so far.

### 30. Sleeping House / Interior Lock

- **Movement speed:** 0.62×
- **Camera sway:** 0
- **Breathing zoom:** 1
- **Weather:** 0
- **Foreground occlusion:** 1
- **Depth:** 3 planes
- **Direction:** Visually intimate. The closing door should physically remove strips of light from plants and pallets.
- **Rule:** No ominous camera movement. The lock itself carries the tension.

### 31. Glasshouse Night → Morning Evidence

- **Movement speed:** 0.55×
- **Camera sway:** 0
- **Breathing zoom:** 1
- **Weather:** 0
- **Foreground occlusion:** 1
- **Depth:** 3 planes
- **Direction:** Night is broken roof ribs, pipe warmth, and near-stillness. Morning is pale bars of light and drawings.
- **Beat:** When identical marks are discovered, stop all camera motion for roughly two seconds rather than zooming.

### 32. Mirror Partition / Exit

- **Movement speed:** 0.70×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 0
- **Foreground occlusion:** 1
- **Depth:** 4 planes
- **Direction:** Light-beam puzzle should produce moving shafts through moisture. Condensation clears progressively.
- **Transition:** Opening the partition introduces colder air and slightly reduced contrast, foreshadowing the dam.

---

## VIII. The Common Supply

The dam is the game's maximum expression of environmental scale and physical force.

### 33. Dam Reveal / Turbine Shed

- **Movement speed:** 0.78× approach
- **Camera sway:** 4 exterior / 1 shed
- **Breathing zoom:** 4 exterior / 1 shed
- **Weather:** 2 — spray
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** The dam reveal gets one of the strongest framing changes in the game: about 0.84× framing. Waterfalls occupy immense vertical area.
- **Visual:** Keep nearby edges sharp; atmospheric whitening increases with distance only.

### 34. Service Ledges

- **Movement speed:** 0.72×
- **Camera sway:** 2 normal; halve on exposed edges
- **Breathing zoom:** 2
- **Weather:** 2–3 — spray and wind
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Large buttresses repeatedly block and reveal the dam. Alternate intimate wet rock with enormous reservoir vistas.

### 35. Reservoir Crossing

- **Movement speed:** deliberately slow rowing
- **Camera sway:** 0 procedural; boat roll supplies motion
- **Breathing zoom:** 3
- **Weather:** 3 normal → 4 during gust
- **Foreground occlusion:** 0–1
- **Depth:** 5 planes
- **Direction:** Major set piece. Normal boat roll may reach about ±1.5°; the central gust may briefly reach ±3°.
- **Rule:** Do not add generic shake. All instability comes from water and hull orientation.

### 36. Far Valve / Storm Galleries

- **Movement speed:** 0.70×
- **Camera sway:** 2 normal / 1 on ropes
- **Breathing zoom:** 2
- **Weather:** 3 → 5 — thunderstorm / squall
- **Foreground occlusion:** 2
- **Depth:** 5 planes
- **Direction:** Lightning exposes distant geometry for one update; it must not blank the screen white.
- **Beat:** During the rope crossing, a flash produces one hard player shadow against the wall.

### 37. Recorder Gallery / Distributor / Pass Exit

- **Movement speed:** 0.64× evidence; 0.76× machinery; 0.82× exit
- **Camera sway:** 0 → 2
- **Breathing zoom:** 1 → 2
- **Weather:** 2 after the storm
- **Foreground occlusion:** 1
- **Depth:** 3 → 5 planes
- **Direction:** Technical investigation remains calm. As the gardens warm and ridge power returns, environmental motion decreases.
- **Beat:** Finish on the tiny mast against opening sky with a mild 0.92× pull.

---

## IX. The Answering Ridge

The ridge progressively removes shelter and then turns the promise of another person into a mechanical self-response.

### 38. Maintenance Shelter / Snow Ascent

- **Movement speed:** 0.55× shelter; 0.74× climb
- **Camera sway:** 0 → 2
- **Breathing zoom:** 1 → 2
- **Weather:** 1 → 2 snow
- **Foreground occlusion:** 0–1
- **Depth:** 3 → 5 planes
- **Direction:** Reduce scenery density with altitude. More sky, smaller vegetation, longer exposures.

### 39. Scarf Post Panorama / Sleet Shelter

- **Movement speed:** 0.70×
- **Camera sway:** 4 panorama / 2 storm / 0 shelter
- **Breathing zoom:** 4 panorama → 1 shelter
- **Weather:** 2 → 5 sleet
- **Foreground occlusion:** 1
- **Depth:** 5 planes
- **Direction:** Panorama should show the interconnected world behind the player. The storm then closes visibility until only one post at a time is readable.
- **Transition:** Perform this contraction continuously rather than cutting to a new backdrop.

### 40. False Signal / Isolator / Chime Crossing / Summit

- **Movement speed:** 1.05× during initial urgency with running animation; 0.60× immediately after the false signal is understood; 0.76× after recovery
- **Camera sway:** 3 → 0 at isolator → 2 crossing; 1 at summit
- **Breathing zoom:** 3 → 1
- **Weather:** 2 — clearing
- **Foreground occlusion:** 1
- **Depth:** 5 planes
- **Direction:** One of the few justified running sequences in the game.
- **Beat:** When the return circuit is isolated and the flashes stop, remove almost all camera motion for several seconds. Summit vista uses quieter 0.90× framing to emphasize doubt rather than triumph.

---

## X. The Original

The world becomes human-sized again while the machinery and evidence reach maximum narrative importance.

### 41. Settlement Streets / Mast Terrace

- **Movement speed:** 0.72×
- **Camera sway:** 2
- **Breathing zoom:** 2
- **Weather:** 1 — damp breeze
- **Foreground occlusion:** 3 — walls and arches
- **Depth:** 4 planes
- **Direction:** Masonry repeatedly hides and reveals streets. Lights behind condensation should repeatedly resemble possible occupancy without confirming it.

### 42. Tower Traversal

- **Movement speed:** 0.68×
- **Camera sway:** 1
- **Breathing zoom:** 1
- **Weather:** 0
- **Foreground occlusion:** 1
- **Depth:** 3 planes
- **Direction:** Hooks, bench, narrow channel, older masonry, and strips of window light create a compact traversal sequence.
- **Character beat:** The small boat crossing should visibly demonstrate learned competence: almost no instability compared with the earlier ferries.

### 43. Cabinet Choice / Original / Far Stair

- **Movement speed:** 0.50×
- **Camera sway:** 0
- **Breathing zoom:** 0–1 only
- **Weather:** 0
- **Foreground occlusion:** 0
- **Depth:** 2 planes
- **Direction:** Remove nearly every ambient effect. The room becomes unnervingly literal. Evidence physically accumulates across the floor.
- **White-handle contemplation:** slight 1.015× push only.
- **Black-handle action:** no camera response at all. Lamps extinguish and reflections disappear; the pantry lamp becomes the only local light.
- **Original reveal:** locked camera. The discovery carries the scene without added effects.

---

## XI. A Door Without a Light

The ending removes the visual grammar that has guided the player through the rest of the game.

### 44. Unwired Street / Children's Shoes / Final Door

- **Movement speed:** 0.58×; 0.50× after setting the fallen shoe upright
- **Camera sway:** 0
- **Breathing zoom:** 0
- **Weather:** 1 — damp night
- **Foreground occlusion:** 0–1
- **Depth:** 3 planes
- **Direction:** No procedural sway. No breathing zoom. No flashes. No signal effects. No dramatic vignette. No automatic camera pull.
- **Lighting:** The pantry lamp makes a small fixed pool of light. The shoes are simply present.
- **Journal beat:** Camera remains still while the player records what happened.
- **Final beat:** At the door, remove HUD and interaction prompts after the knock. The knock has no pattern. Hold the final composition without camera movement.

---

## Detailed visual specification

These are **art and composition requirements** for the playable sets above. They are intentionally more specific than the effect presets. They describe what should actually be visible on screen: the dominant silhouette, foreground obstruction, playable surface, middle-distance structure, far depth, lighting logic, evidence placement, and the image that should carry the transition into the next set.

### Shared visual language

- **Foreground / observer plane:** sparse, very dark, large forms close to the camera. Use these as occasional occluders rather than decorative borders. A foreground object should look as if the unseen observer could reach out and touch it.
- **Playable plane:** sharpest edges and highest material detail. The protagonist, contact surfaces, ropes, ladders, rails, boats, mechanisms, and evidence must read immediately against this plane.
- **Middle planes:** two or three distinct silhouette families with progressively lighter tone and reduced detail. Never collapse them into one fog-gray strip.
- **Far plane:** pale but still shaped. Hills, towers, ridges, or large engineering works should retain a recognizable contour even when atmospheric depth is strong.
- **Sky / fog / open water:** reserve the lightest values for empty distance, reflected light, and openings. White should describe space, not wash out the world.
- **Edges:** near silhouettes are crisp. Diffusion belongs just outside distant silhouettes or in spray/fog, not across the object itself. The visual target is **sharp forms inside diffuse atmosphere**.
- **Terrain:** no generic floating platform language. Every traversable surface must belong to a plausible hill, bank, roof, embankment, quarry bench, greenhouse terrace, dam gallery, or constructed floor.
- **Scale:** when the narrative says the world is larger than the protagonist, show it literally. Make trees, buildings, cranes, waterfalls, and rock cuts dominate the frame rather than merely shrinking the character.
- **Narrative clearance:** preserve a clean readable band for prose without placing the protagonist, rope contacts, boat hull, critical terrain lip, evidence object, or primary landmark behind it.
- **Object history:** wear, water marks, rust, soot, rubbed paint, broken glazing, stretched cloth, footprints, polished handholds, and repairs should make every inhabited structure look used rather than procedurally placed.

### 1. Kitchen Window — exact visual target

- **Composition:** frame the kitchen as a shallow box. The sink/window occupies the visual center; the protagonist stands close enough that her reflection overlaps the missing landscape outside. The room should feel ordinary and slightly cramped, not Gothic.
- **Foreground:** dark edge of the counter, basin rim, one hanging cloth, and the protagonist's arm/shoulder. Avoid foreground clutter.
- **Playable / middle plane:** wet cup beside the sink, the shelf with the sister's chipped cup, the table and chair, and the stove area. The sister's slippers should be visible only when the player turns away from the window or begins packing.
- **Exterior:** garden wall first, then rake and bucket, then the nearest apple branches. Beyond that, almost nothing: the fog should physically erase the hills rather than render a generic gray background.
- **Lighting:** flat cold evening light through the glass; interior objects are darker than the fog. The three flashes momentarily create a tiny hard highlight on wet glass and the cup rim.
- **Surface detail:** condensation streaks, greasy wiped crescent on the pane, cold water shine, chipped ceramic edge, soap film.
- **Transition image:** as the player leaves, the final view back should show the kitchen reduced to a small rectangle of flat light with the chair pushed under the table.

### 2. Tall-Grass Memory — exact visual target

- **Composition:** grass should tower over both children, creating walls and a ceiling of seed heads. The secret room is not a cleared arena; it is a body-sized depression where stems bend outward and close again behind movement.
- **Foreground:** several black or charcoal grass blades cross the lens at different heights, sometimes hiding a face or hand for a fraction of the scene.
- **Playable plane:** flattened green stems, bare soil patches, beetle/seed-pod scale details, sister seated or crouched opposite the narrator.
- **Middle plane:** dense vertical stems with occasional gaps revealing brighter field beyond. The pocket mirror should be small enough that the flash feels startling rather than like a lamp.
- **Far plane:** only a suggestion of the house/upstairs window through the grass when the wrong signal is remembered.
- **Lighting:** brightest and most organic light in the opening chapter. Use soft broken patches through moving seed heads rather than a uniform glow.
- **Motion:** grass bends in coherent gusts with local lag; loose strands at the sister's temples move less than the upper grass.
- **Transition image:** the remembered dark upstairs window should align visually with the present-day kitchen window before returning to the fog.

### 3. Pre-dawn Departure / Orchard — exact visual target

- **Composition:** start inside the doorway looking outward, then progressively put the house behind the protagonist. Domestic geometry should physically shrink with each short segment of travel.
- **Foreground:** door frame and latch for the first seconds; the caught thread between coat and iron must be readable at close range.
- **Route:** garden path → wall opening → orchard edge → ditch under alders → root-soft road. Keep it continuous rather than teleporting between biomes.
- **Middle plane:** apple trunks, low stone boundaries, wet ditch, hedge gaps, boundary stones.
- **Far plane:** one pale chimney above the orchard should remain visible longer than expected, then disappear behind a bank.
- **Lighting:** nearly colorless dawn. Wet edges catch the light, but there should be no romantic sunrise.
- **Material cues:** dark damp soil, leaf litter stuck to stone, rough coat cloth, bare winter/late-season branch structure.
- **Transition image:** the last piece of architecture disappears and the road surface becomes roots; that visual substitution is the actual entrance into the forest.

### 4. Deep Forest / Marked Tree — exact visual target

- **Composition:** use enormous trunks as vertical architecture. The player should rarely see a complete tree; crowns disappear above the frame and into fog.
- **Foreground:** two or three near trunks with deep bark folds can wipe across 25–35% of the image. One should briefly hide the path and reveal it rearranged.
- **Playable plane:** continuous rooted earth with wet leaf skin, exposed pale roots, russet bracken, mossy boulders, shallow black water in hollows.
- **Middle planes:** gray trunks at different spacings, then a long hill swell that appears and disappears with fog.
- **Marked tree:** hooked branch with the wool caught on it; the cloth is folded into a cut in bark. The older grown-over wedge, date, and socket mark must be spatially connected, not presented as disconnected UI clues.
- **Lighting:** mostly diffuse overhead white with occasional brighter openings where a missing limb or crown lets sky through.
- **Motion:** intermittent drops, a localized gust bringing silver-backed leaves down from deeper canopy.
- **Transition image:** after the player looks back, the entrance path should be genuinely hard to identify between black trunks.

### 5. Fallen-Tree Hollow — exact visual target

- **Composition:** the obstacle is a natural depression, not a platform puzzle. One bank should be higher and loose enough that climbing it directly looks implausible.
- **Foreground:** damp root fans and a partial trunk edge close to the observer; keep them irregular rather than framing the screen symmetrically.
- **Playable plane:** brown water at the bottom, churned leaf mold on approach, exposed roots, the dead leaning tree whose sawn/partly severed root mass still grips the bank.
- **Tree detail:** cracked bark plates, rotten red-brown cubes under the outer bark, old saw damage, splintering roots. The trunk must look heavy enough that the struggle is credible.
- **Background:** forest depth continues beyond the hollow so this does not feel like a self-contained puzzle room.
- **Animation image:** before release, almost nothing moves except the protagonist and slight root flex. Once a root gives, the whole trunk should rotate slowly, then accelerate and drag smaller branches through the pale opening.
- **Landing:** debris settles after impact—small twigs, soil, leaves—while the large form becomes a believable bridge.
- **Transition image:** the fallen trunk points the eye toward the low mill hollow beyond.

### 6. Mill Hollow — exact visual target

- **Exterior composition:** put the mill low between hills so the player descends into it. A leaf-covered pool occupies one side; the leaning building grows out of fern/ivy rather than sitting on a clean pad.
- **Foreground:** wet fern fronds, dark bank edge, or a branch that temporarily crosses the lower frame.
- **Mill silhouette:** sagging roof, one gutter with saplings, timber boards black at the foot and lighter under the eaves, half-hidden stationary wheel.
- **Water:** thin spill threads around boards should resemble cracks in glass. The pool reflects roof and leaves but is repeatedly broken by silt trickles.
- **Interior:** a broken-shutter entry into a mostly empty room. The sloping desk, register, and broken window grid are the composition. The first bright scrape in dust from moving the desk must be obvious.
- **Lighting:** one strong rectangular window beam with the rest of the interior falling into calm mid-gray/black.
- **Evidence staging:** the register should lie where the light catches only part of the ink until the player moves the desk.
- **Transition image:** leaving the mill, the uphill path brightens and the forest thins toward an exposed ravine.

### 7. Ravine → Clearing → Gate → City Reveal — exact visual target

- **Ravine:** living branch overhead, rope tied around it with visible twists and knot wraps. The branch must bend under body weight; below, water/sky is so distant it is hard to read which.
- **Clearing:** remove most near vertical trunks in one step. Present layered hills behind hills, each lighter and slower than the last. The path becomes a single dark stitch across open pale ground.
- **Bell:** small wooden bell on a branch at the clearing edge; keep it humble and domestic against the huge view.
- **Gate:** three substantial wooden/iron shafts embedded in an old frame. Grease, hand wear, and mechanical coupling should make the puzzle feel built for repeated use.
- **City reveal:** the gate must not open onto a generic skyline. Upper floors emerge from fog while gullies still contain trees; fences and drains mingle with roots. Near tower faces are almost black, distant ones nearly white.
- **Scale:** hold long enough to show that the city continues the same hills rather than replacing them.
- **Transition image:** the last forest branches remain in the foreground while the first roof surfaces take over the route.

### 8. Service Terrace / First Rooftops — exact visual target

- **Composition:** the city begins above and below the player simultaneously. Lower stories vanish into fog, leaving roofs as islands with no visible streets connecting them.
- **Foreground:** soot-dark masonry corner, broken balustrade, drainpipe, or parapet edge close enough to obscure a third of the view.
- **Route:** abandoned service terrace with standing water, lowered ladder, rubbed door bolt, narrow roof passages.
- **Middle planes:** chimney cages, aerials, water tanks, exposed wall remnants, cables crossing gaps and disappearing into fog.
- **Far plane:** towers at three or more depths, with tops harder than bases. Do not draw a single skyline silhouette.
- **Material cues:** soot, cracked render, rust streaks, wet grit, patched flashing, old hand-polished stone.
- **Lighting:** broad overcast sky; the fog below should be brighter than rooftops so drops feel genuinely deep.
- **Transition image:** climbing the first ladder should erase the forest road completely.

### 9. Schoolroom / Rooftop Route — exact visual target

- **Schoolroom:** rows of undersized desks, dark leak blooms in the ceiling, a map held flat by four bricks, cups of dead stems, half-erased blackboard. The room should feel abandoned abruptly rather than ruined theatrically.
- **Window composition:** every important roof on the sister's route should be visible from this one room at different depth levels.
- **Route map:** physically folded, broken along the repeated route line; charcoal path should correspond to landmarks visible through the window.
- **Exterior continuation:** roof grit, broken nests, shallow tank reflections, exposed wallpaper on a vanished neighboring room, coping worn by hands.
- **Scale:** inside, protagonist is large and boxed by child-sized furniture; outside, she becomes small against gaps and tower walls.
- **Lighting:** soft rain light through broad windows. The map gets the brightest flat plane in the room.
- **Transition image:** the first high gap should line up visually with the route mark just traced on the map.

### 10. High Crossings / Rain Tank — exact visual target

- **Composition:** rooftops become narrower and more exposed. Keep the player close to a wall while open courts drop away on the opposite side.
- **Foreground:** rain-slick parapet or tank support legs crossing the frame; no decorative rails where the novella describes exposure.
- **Water tank refuge:** the iron belly should dominate the upper frame, with hollow rain impacts implied by visible vibration/rivulets. Its ladder ends at a secured hatch.
- **Opposite signal:** place the flashing window across a narrow court so the destination looks close while the actual route climbs and snakes away.
- **Street reveal:** when fog opens, show one overturned cart, shuttered shops, and pale sheet material moving in water far below. It should disappear before the player can fully study it.
- **Rain:** use diagonal sheets near camera, vertical streaks farther away, and bright beads on rails. Do not reduce building edge sharpness.
- **Transition image:** the player arrives at the flashing window only to see a mechanically empty room beyond the glass.

### 11. Signal Room / Western Relay — exact visual target

- **Signal room:** almost bare. Chair centered toward the window, wall-mounted lamp, toothed wheel, metal tongue, hook with no coat. Empty floor area is important.
- **Mechanism:** the flash source must be visibly autonomous once inspected—wheel teeth, pivot, oil bead, tongue lifting and falling.
- **Evidence:** watch log lies within reach of the empty chair; duplicated signature blots should be inspectable without floating overlays.
- **Lighting:** each flash briefly whitens chair, floor, and the protagonist's handprint on outer glass. Between flashes the room falls back to gray.
- **Relay exterior:** after leaving, reveal westward roofs stepping down into warehouses, fenced yards, sidings, and flatter country.
- **Relay cabinet:** scorched crossover, blackened wire path, three contact banks. When solved, a row of lower windows should illuminate in perspective.
- **Transition image:** those newly lit windows lead the eye out of the vertical city toward the flat pump country.

### 12. Pumpjack Plain — exact visual target

- **Composition:** radically widen the horizon after the city. Pumpjacks occupy different distances and orientations but remain completely stopped.
- **Foreground:** one close service lamp, low grass, or pipe riser; avoid tall occluders so exposure becomes the dominant feeling.
- **Playable plane:** compacted service road with puddled tire ruts and sparse grass invading concrete pads.
- **Middle planes:** pumpjacks with lowered horseheads, counterweights frozen at different arcs, small sheds with faintly burning lamps.
- **Far plane:** low weathered ridges and storage tanks; the city survives only as pale thin uprights on the eastern rim.
- **Materials:** peeling paint, bright polished slots where rods once moved, rust scales, cold iron, wind-flattened grass.
- **Lighting:** high flat daylight with hard small metal highlights. Service lamps barely stain the ground yellow/white.
- **Transition image:** route lines and pipes begin converging toward the tank basin.

### 13. Tank Basin / Service Cut — exact visual target

- **Composition:** tanks should feel enormous because the route passes close to their curved walls. Use the curvature to hide and reveal the next yard.
- **Foreground:** tank wall, pipe elbow, or fence mesh may perform slow wipes across the scene.
- **Ground:** terraced basin with eroded channels, oily puddles, washed culverts, buried pipe humps, older foundations trapped inside newer fences.
- **Tank detail:** vertical rust tears, seam bands, ladder shadows, pooled water at the concrete ring, thin iridescent-looking dither pattern in puddles without implying color.
- **Service cut:** earth rises above the protagonist's head; exposed roots hang from clay/stone layers and pump heads alone break the skyline.
- **Culvert obstacle:** round stone lodged in the broken channel, scraped bank foothold, visible slight shift under weight.
- **Weather:** grit should travel along the curve of a tank before escaping across the yard.
- **Transition image:** the service cut funnels directly toward the small dark stove shed.

### 14. Stove Shed — exact visual target

- **Composition:** narrow rectangle with the stove slightly off-center. The room should feel arranged by people who expected another cold night.
- **Walls:** soot silhouettes where coats used to hang; one bench polished smooth at the edge; a few bolt/rivet lines, no decorative clutter.
- **Wood pile:** sorted by thickness with shaved kindling on top. This precise order should be one of the most visually legible details.
- **Stove:** cold iron, openable door, pipe entering wall, cloth-wrapped wire binding at the loose section.
- **Micro-details:** hard gray ash crust, one charred piece holding its shape, curled potato skin, shallow dish of rainwater whose rings react to knocks.
- **Floor:** iron-joist boards and the recessed hatch catch under the bench; it should look physically inaccessible rather than game-locked.
- **Lighting:** doorway crack and one wall gap; most edges are black-on-midgray.
- **Transition image:** leaving the door open produces a bright vertical slit behind the player as the route returns outdoors.

### 15. Distribution Station / Eighth Line — exact visual target

- **Composition:** low open station rather than another interior room. Awning, tied empty cans, service wall, three measuring vessels, and the rail embankment beyond should all fit in one readable composition.
- **Evidence:** two lists rolled inside the last can; when opened, the blank eighth line and the already-entered initial should be physically side by side.
- **Vessels:** clearly different capacities—8, 5, and 3—through height/diameter, not just labels. Pipes/taps should make transfer paths obvious.
- **Foreground:** low wall or can row, never enough to hide the puzzle.
- **Background:** pump field recedes behind; first rail embankment cuts cleanly across the horizon.
- **Lighting:** late light increasingly makes service lamps meaningful.
- **Solve image:** when four measures balance in the two larger vessels, lamp brightness steadies progressively down the line.
- **Transition image:** the player leaves before the lamps can become dominant; the railway takes over the horizon.

### 16. Carriage Night → Rail Yard Dawn — exact visual target

- **Night carriage:** wheel-less passenger body on timber blocks beside the track. Broken window entry, wet stain on one bench, fallen ceiling lining exposing ribs like an inverted hull, split hanging strap.
- **Foreground:** seat back and window frame form a quiet enclosure; boots under the bench point into the aisle.
- **Outside at night:** signal gantry divides the sky, but no lights or motion imply a train.
- **Dawn reveal:** clear air removes fog-based depth and replaces it with long linear perspective: three cuttings, goods shed, lifted siding traced by birches, intact locomotive on a parallel track.
- **Landslide:** far cutting buried by a huge mixed mass of rock, soil, roots, and whole trees; rails visibly enter and do not emerge.
- **Locomotive:** complete enough to feel wrong in its stillness—connecting rods, couplings, forward lamp, plant growing onto the cab step.
- **Transition image:** the station building becomes the next dark mass beside the otherwise open rail geometry.

### 17. Station / HOME Ticket — exact visual target

- **Composition:** benches face a window/door opening onto the blocked tracks. Keep the waiting-room geometry plain and functional.
- **Focal object:** ticket marked HOME pinned to a noticeboard at eye level; its punched hole and fold should read on close inspection.
- **Evidence cluster:** cancellation book on a desk/shelf below, weather warning above or beside it so the player can visually compare all three dates.
- **Background:** through openings, the landslide remains present as a white scrape and angled trees; never let the room isolate the story from the physical cause.
- **Benches:** enough visible length for seven people plus an obvious extra small space, without literal ghost silhouettes.
- **Lighting:** clear morning side light, long static shadow lines, no fog.
- **Transition image:** exiting west frames the broken trestle and dangling service rope.

### 18. Broken Trestle / Signal Cabin — exact visual target

- **Trestle:** supported section ends abruptly in open air. Sleepers overhead while the protagonist traverses lower bracing; wet bolts and repeated steel triangles create the visual rhythm.
- **Depth:** broken timbers and ditch below, intact rails above, service rope on the far gantry. Keep the gap visibly open throughout traversal.
- **Cloth:** small wool strip wound around a splintered brace, integrated into the crossing route rather than centered like a collectible.
- **Signal cabin:** narrow elevated room with windows down all three routes. Lever bank forms the main foreground silhouette.
- **Details:** polished lever handles, numbered brass ovals, corrected route map, child’s five-legged dog wrapped around one handle.
- **Lighting:** dry, pale daylight; cabin interior slightly darker so exterior track lines remain readable through glass.
- **Transition image:** from the cabin, the player should first see the three wagons blocking the western service road.

### 19. Shunting Yard → Marsh Transition — exact visual target

- **Rail puzzle:** three unmistakably different bodies—tank wagon, short passenger body, heavy brake truck with large rusted handwheel. Sidings and loop geometry must be visible enough to plan movement spatially.
- **Ground:** real ballast/embankment, weeds between sleepers, buffers anchored into earth. No floating rail segments.
- **Mass:** wagons should visually compress springs/couplers or rock slightly when stopped, reinforcing weight.
- **Background:** locomotive and signal cabin remain behind at first; then sheds thin out.
- **Transition:** ballast gradually mixes with grass, then reeds. Rail drainage becomes open water. Telegraph/gantry verticals give way to willow/dead-trunk verticals.
- **Water:** first still pools should reflect the evening sky before the player reaches the actual ferry.
- **Final image:** rails disappear under vegetation while the channel curve becomes the new directional line.

### 20. Ferry Bank / First Boat Crossing — exact visual target

- **Opening composition:** view through dense reeds to the far-bank boat. Canvas shoulder-shape and pale oar should genuinely read as a seated human for a moment.
- **Near bank:** mud lip, mooring post, rope entering black water. Pulling the rope draws the boat through its own reflection.
- **Boat:** broad enough for several people and baggage; pale wet seating arcs, repaired plank, cup under middle seat, canvas over stern.
- **Water:** very dark close to the hull with pale sky reflections farther out. Each oar stroke briefly creates a smooth oval before reflection closes.
- **Foreground:** reeds cross both player and reflected boat; some nearer stems nearly black, farther ones gray.
- **Far bank:** low root overhang with hanging fringe and the roofless shelter behind it.
- **Motion:** hull settles slowly under weight. Camera motion follows water and hull only.
- **Transition image:** bow touches the far bank almost silently, with the roofless walls framing the next scene.

### 21. Raised Marsh Path — exact visual target

- **Composition:** narrow raised bank through an environment with no single reliable edge. Water, sedge, mud, and reflected sky should continuously swap visual roles.
- **Foreground:** willow trunks/branches and reed clusters make intermittent observer occlusion.
- **Playable surface:** compacted path with roots where firm, soft cut-throughs where water has eaten it away.
- **Left side:** old reeds thatch a ditch; **right side:** sedge transitions to mud, shallow transparent water, then dark channel.
- **Middle plane:** rafts of twigs at willow feet, weathered fence posts, small islands, dead trunks doubled by reflection.
- **Far plane:** pale escarpment/quarry only distinguishable once a dark seam is visible across it.
- **Lighting:** passing cloud should visibly convert clear shallows into mirror without altering geometry.
- **Transition image:** the awning and waiting bench appear as the first deliberately constructed human rest point in the marsh.

### 22. Waiting Awning — exact visual target

- **Composition:** long bench under a simple cloth/wood awning, one chair sunk slightly into ground, water channel beyond. The space should feel made for people to recover, not like a checkpoint kiosk.
- **Evidence of bodies:** walking-stick groove worn round at one end; three sets of shoe scuffs, smallest ending halfway down; sackcloth cushion.
- **Rope/post:** thick rope knot resting against post, oval wear mark from repeated load, cut end patiently whipped with thread.
- **Bell:** five water marks on bank board, bell rope beside them, extra low loop for a child.
- **Background:** quiet channel, reeds, a few distant birds; no dramatic landmark.
- **Lighting:** overcast, low contrast, but edges remain sharp.
- **Bell reaction:** one ripple/bird/reed response should travel away from the player and die without return.
- **Transition image:** upper lock masonry appears beyond the flat marsh as the first strong geometric structure ahead.

### 23. Lock Chambers — exact visual target

- **Composition:** two adjacent rectangular chambers with clearly different levels and a ferry cradle between them. The player should understand vertical water movement from the picture alone.
- **Walls:** three historical water bands—powdery upper stone, black wet band, green threads below. Ladder crosses all three.
- **Mechanism:** old blue-painted pump with surviving paint only in protected corners; middle gate thick enough to look heavy.
- **Boat/cradle:** small, workmanlike, visibly floating rather than resting on a hidden platform.
- **Background:** flat marsh stays low beyond walls; distant quarry remains a pale mass.
- **Motion:** as levels equalize, reflections rise/fall against fixed masonry lines. When equal, violence disappears and water becomes nearly still.
- **Arrival image:** upper wall sinks relative to the player until grass tips and raised path appear above it.
- **Transition image:** from the upper landing, hold a backward vista over every marsh set already crossed before turning toward quarry.

### 24. Upper Marsh / Quarry Reveal — exact visual target

- **Backward view:** roofless shelter, awning, tiny bank strip, lock chambers, and the channel should all be visible at once but reduced by distance.
- **Foreground:** drying mud on the player/path edge and a few high grasses; avoid dense reeds now that elevation has increased.
- **Forward view:** quarry first reads almost like bright cloud because its stone is pale. A dark seam and terrace shadow finally disclose it as missing hillside.
- **Composition:** the route should climb gradually so the quarry occupies more vertical screen space without a hard reveal cut.
- **Atmosphere:** clearer than marsh; less reflection, more dry air and exposed mineral surfaces.
- **Scale:** by the end, remaining trees on the quarry rim should look absurdly small relative to the cut.
- **Transition image:** the first white spoil fan enters the playable foreground.

### 25. Quarry Floor — exact visual target

- **Composition:** one side of a hill is simply absent. Use stepped white terraces across most of the background, each with a thin black undershadow.
- **Foreground:** angular chips, powder, discarded chisel, one larger weathered stone. Keep enough open ground that the protagonist remains readable.
- **Middle plane:** spoil fans, standing water along a bench tilt, seepage line turning pale stone dark, diagonal ramp climbing the cut.
- **Far/high plane:** crane on upper works with cable dropping across multiple terraces; a few original ridge trees cling above exposed roots.
- **Material detail:** drill holes, wedge marks, fractured beds, tiny snail shell, chips sorted by wind.
- **Stone obstacle:** barrel-sized rock in a pre-cut groove should visibly belong to the terrain and become a plausible step only after dropping into the hollow.
- **Dust:** broad sheet movement follows gusts and ledges.
- **Transition image:** ladder recesses and small bootprints draw the eye upward.

### 26. Ladder / Ledge Ascent — exact visual target

- **Composition:** repeated changes between close stone face and sudden high views. Do not show the entire climb at once.
- **Playable wall:** irregular white/gray cut rock with iron rungs set at inconvenient tall-person intervals.
- **Human traces:** three small bootprints in old lime among adult tracks; cloth strip tied at child shoulder height as a makeshift edge boundary.
- **Foreground:** occasional rock shoulder crosses the observer plane; no arbitrary black vignette.
- **Background vistas:** marsh becomes silver strip; oil tanks become dull coins; railway is only a line. The player should recognize distance through transformed scale.
- **Lighting:** cloud changes should reveal stone grain without flattening terrace shadows.
- **Wind:** powder lifts from specific shelves before larger gusts arrive.
- **Transition image:** upper works, hoist frame, and hanging cage become the first moving-capable machinery since the railway.

### 27. Hoist / Shaft Descent — exact visual target

- **Upper works:** cage over black shaft, pulley frame, two unequal balance arms, brake drum, six identical iron weights. Nothing should be decorative.
- **Handle:** split wood bound with the sister's scarf; this should be the warmest-textured object in an otherwise mineral/metal composition.
- **Evidence:** torn note trapped under loose cage floorboard, visible only after entering/looking down.
- **Shaft:** tight vertical slice of damp stone. Nearby wall moves past the player; dark seams, fine roots, water tracks, and one tiny fern provide scale.
- **Cage:** rough replacement floorboard, open gaps to darkness, brake cord beside frame. Slight tilt is physical, not a camera effect.
- **Lighting:** bright quarry square above shrinks steadily; lower gallery is a dim horizontal opening.
- **Transition image:** on exit, the quarry becomes a bright doorway behind while green ferns and broken glazing appear ahead.

### 28. Lower Gallery / Glasshouse Reveal — exact visual target

- **Opening:** begin with damp narrow gallery, chisel marks, thread of water on floor reflecting the distant quarry entrance.
- **Color-value shift:** introduce darker vegetation and warmer mid-grays gradually, not an instant scene palette change.
- **First glasshouse forms:** curved iron ribs step down sheltered terraces; many panes missing, remaining panes catching pale sky in small angular reflections.
- **Foreground:** ferns and thorny growth push into the corridor before the player fully enters the gardens.
- **Middle plane:** broken greenhouse frames overlap one another, creating repeating arches that replace quarry terraces as the depth motif.
- **Atmosphere:** a faint suggestion of warm air/condensation near the first doorway.
- **Transition image:** the player parts thorns and finds visibly living beds inside.

### 29. Living Glasshouse Beds — exact visual target

- **Composition:** three planted rows stepping with hillside beneath curved ribs. This is abundant but maintained life inside ruin, not jungle.
- **Foreground:** a few broad leaves and rib members cross the observer plane; keep the player visible through gaps.
- **Beds:** warm dark soil, shallow watering channels, strings over seedlings, cut-back ivy, broken pot sheltering a new shoot.
- **Work area:** watering can on bricks, basin with mineral mound from repeated drips, cups, low pipe sweating where warm return enters cooler air.
- **Structural detail:** surviving panes, empty glazing clips, limewashed brick green at the foot, gutter damage, felt-wrapped pipe joints.
- **Micro-motion:** one drop at a time, leaf rebound after touch, condensation growth/sliding, slight warm-air leaf turn.
- **Lighting:** low sunlight threads through broken panes in narrow bars; soil beneath benches remains black.
- **Transition image:** seven sleeping places appear between beds, shifting the scene from horticulture to habitation.

### 30. Sleeping House / Interior Lock — exact visual target

- **Composition:** seven straw pallets raised just off warm soil; sacking curtain creates an imperfect private room at one side.
- **Human objects:** basin, stiff towel, comb missing teeth, one crooked weighted curtain corner. These must feel used and specific.
- **Door:** heavy iron lock box on the inside, missing key, wedge currently holding the door open. The floor shows a broad repeated scrape through dirt.
- **Lighting:** door opening provides a bright vertical band across nearest leaves and pallet. As it closes, that band physically narrows and plant highlights disappear.
- **Temperature cue:** visible condensation/pipe sweat and the contrast between warm interior plant movement and cold exterior stillness.
- **Evidence:** keeper inventory on workbench rather than floating near the lock.
- **Transition image:** after the player restores the wedge, the view settles on rows of seedlings and their count slates.

### 31. Glasshouse Night → Morning Evidence — exact visual target

- **Night composition:** protagonist lying low among straw; broken greenhouse ribs silhouette against one patch of sky. No large landscape view.
- **Motion:** pipe water murmurs through tiny irregular surface cues; cooling glass makes isolated droplet shifts; one leaf turns in warm rising air.
- **Lighting:** almost all illumination comes from residual sky through broken roof. Keep the sleeping area dark enough that small reflective glass edges matter.
- **Morning:** long pale bars cut across beds; lower panes are opaque with condensation.
- **Evidence cluster:** two count sheets at the clear pane; jointed-arm maintenance tag in drawer; child's lantern drawing beside the older printed card.
- **Drawing detail:** child version includes uneven figure spacing, rubbed/redrawn person, extra fingers, smallest figure leaning against the woman. Printed card is unnaturally regular.
- **Beat:** identical correction marks should align physically when held to light.
- **Transition image:** the child's lantern rays guide the eye toward the mirror mechanism at the rear partition.

### 32. Mirror Partition / Exit — exact visual target

- **Composition:** glass partition blocks the route, lower receiver beaded with moisture, upper shutter/vent above head height.
- **Mirrors:** tarnished around edges, mounted on practical brackets, visibly redirecting one coherent beam rather than magical light.
- **Beam:** use sharp bright core with dithered dust/moisture halo. A falling drop crossing it should flash as a tiny event.
- **Condensation:** clearing begins near airflow and spreads irregularly, revealing the route beyond in fragments before becoming readable.
- **Plants:** first row bends slightly as cold air enters; the environmental response confirms the vent changed something.
- **Background beyond glass:** transition from curved ribs/foliage to harder service pavement and colder open terrain.
- **Transition image:** as the lower latch opens, the first distant dam structures should remain partly obscured so sound/scale arrives before full sight.

### 33. Dam Reveal / Turbine Shed — exact visual target

- **Approach:** before the dam appears, puddles and railings should visibly tremble; spray beads exist where there is no rain.
- **Reveal composition:** wall spans nearly the entire valley width. Four broad spill sheets descend from near the crown. Reservoir is only a pale strip above; basin and valley continue below.
- **Scale references:** ladders and galleries should be tiny but sharp; far mountains stack behind with one snow notch. This prevents the dam from reading as a flat backdrop.
- **Water:** each spill sheet has a hard top edge, thick corded middle texture, and diffuse spray only at its foot.
- **Foreground:** pitted parapet pebble/rail and occasional wet masonry edge.
- **Turbine shed:** vibrating interior, lamp over gauge with mica replacement, scratched duplicate needle lines, diagram showing feed splitting to lamps and recorder.
- **Lighting:** white water is not pure blank white; preserve internal ridges so movement reads on e-paper.
- **Transition image:** service route leaves the shed and begins climbing along rock beside the reservoir.

### 34. Service Ledges — exact visual target

- **Composition:** alternate between tight rock-and-masonry corridors and sudden reservoir exposures. Buttresses should repeatedly erase half the dam and then reveal it.
- **Playable route:** wet stone ledges, small masonry bridges, ladders bolted into walls, overhangs that visibly shelter sections from rain/spray.
- **Organic detail:** mineral teeth under ledges, ferns in seams, tiny round leaves, one shrub rooted sideways above the path.
- **Depth:** reservoir surface far below, opposite wall/landing small enough to create anxiety about the coming boat.
- **Foreground:** near buttress or rock overhang may occlude the player momentarily, but contact edges remain readable.
- **Lighting:** moving spray veil changes contrast only over distant water; rock near the player stays crisp.
- **Transition image:** descending steps reveal the narrow maintenance boat chained at the lower landing.

### 35. Reservoir Crossing — exact visual target

- **Boat:** narrow dam-worker craft with dark repair plank, split gunwale bound in cloth-covered wire, single pair of heavy oars, shallow center.
- **Opening scale:** once away from the wall, tiny doorways and ladders shrink visibly above the player; the wall seems to rise because perspective changes.
- **Water:** large slow ridges from spill turbulence pass under the boat after a delay. The surface should carry both broad reflections and local dark gust patches.
- **Spillway:** nearest sheet resolves into thick translucent-looking grayscale cords before exploding into spray. A leaf/bark fragment vanishing into it sells force.
- **Far landing:** partially hidden by buttress, appears/disappears behind wave motion. Keep it small but precise.
- **Gust event:** darken/roughen one patch of water, yaw the hull, let one wave fold over the gunwale, move the bag physically against the ankle.
- **No shake:** the dam remains massive/stable while the tiny boat misbehaves.
- **Transition image:** chained boat continues rising/falling beneath the far steps while the player climbs away.

### 36. Far Valve / Storm Galleries — exact visual target

- **Valve landing:** scraped fingers, wet clothing, notebook spread under shelter, broad cold pipe entering the next chamber. The route label should be physically obscured by lime until cleaned.
- **Valve chamber:** large wheel with sister note tied to it, maintenance box holding keeper account/request. Make the two documents share the same physical surface when compared.
- **Storm build:** first bright seam in cloud, then sharply revealed distant gullies, then thunder/rain. Do not gray out the world uniformly.
- **Gallery climb:** ladders with small water pools at welds, open wall faces where spray blows upward, sheltered overhang sections with calmer surfaces.
- **Rope span:** doubled knot around iron eye; worn pale fibers where hands pass.
- **Lightning beat:** single frame/update of hard geometry and protagonist shadow on the wall. No repeated strobe.
- **Transition image:** metal recorder cabinet appears in a relatively dry recess after the storm's visual maximum.

### 37. Recorder Gallery / Distributor / Pass Exit — exact visual target

- **Recorder recess:** dry rubber-sealed cabinet amid wet galleries. Manual pages should look almost new because they are protected.
- **Diagram:** jointed writing arm, master plate, selector, two interlocked faces. Keep it legible as an engineering drawing integrated into the world.
- **Distributor:** pipe network and gauge become the dominant geometry. Cold return should visibly carry condensation; as it primes, that wet line retreats down the pipe.
- **Mechanical response:** deep fill event implied through pipe vibration and one relay beginning to click behind wall.
- **Lighting:** after storm, air is colder/clearer. Reservoir and opposite landing can be seen farther than before.
- **Exit:** gallery gives way to climbing path. The dam falls away behind while the first mast is a tiny dark pin against a narrow pale sky opening.
- **Transition image:** the mast becomes the sole vertical landmark as industrial masonry ends.

### 38. Maintenance Shelter / Snow Ascent — exact visual target

- **Shelter interior:** wall-fixed bunk, washbasin with thin ice, sparse hooks/shelf. Damp clothing should be darker than everything else; stockings on hands are visually awkward and human.
- **Outside:** new snow lies selectively—grass still pierces thin cover, hollows look suspiciously smooth, overturned stones show dark undersides.
- **Vegetation gradient:** normal trees → shortened wind-cut clumps → crouched shrubs below boot height. Let this change occur over travel distance.
- **Ravine line:** one hanging crossing line over a narrow dark cut; keep it functional rather than spectacular.
- **Rock:** silver weathered faces with fresh snow only on upward surfaces.
- **Sky:** occupies progressively more of the frame as the climb continues.
- **Backward view:** dam is eventually hidden under cloud while its sound persists.
- **Transition image:** larger scarf fragment tied beneath a guy wire marks the next authored stop.

### 39. Scarf Post Panorama / Sleet Shelter — exact visual target

- **Scarf post:** weathered shelter post, old guy wire, large scarf fragment with frayed outer edge but tight weave where protected. Tracing sits beneath metal cap.
- **Panorama:** one of the game's widest coherent views. Show descending water system: high streams/ridges → dam cloud → gardens/quarry area → marsh flats → distant industrial/city uprights where possible.
- **Foreground:** exposed ledge and protagonist knee/hand on rock during gust; no decorative vegetation.
- **Weather contraction:** sleet first as discrete grains, then diagonal density that successively erases far, middle, then near planes until only next post and local rock remain.
- **Shelter:** rock-shoulder lee, slab roof, black gaps where stones shifted, narrow shelf with papers weighted by flat stone, old lamp burn on floor.
- **Lighting:** after sleet stops, roof seams admit pale uncertain light.
- **Transition image:** higher ridge reappears, then three flashes puncture the newly opened gray distance.

### 40. False Signal / Isolator / Chime Crossing / Summit — exact visual target

- **Initial run:** signal mast/ridge remains visually fixed ahead while near rocks and posts accelerate past, making urgency readable without extreme camera effects.
- **Warning post:** wet rotating tin with sister's scratched warning on one face and return-circuit drawing on the other. It must visibly catch the distant flash at the same interval.
- **Cable:** traceable line along path with fresh repair bindings, leading to an ice-stiff isolator housing.
- **Isolator:** small silver contact gap becomes the entire focus after disconnection. The world should stay still long enough for absence of the next flash to become visible.
- **Chime stand:** four covered chimes/rods matching post heights; mechanical crossing latch releases at distance.
- **Crossing:** sleet-broken footprints on boards, dark lamp glass behind, no figure-shaped staging near the mast.
- **Summit:** quiet, sparse, cloud bands over country; one tiny brightness appears and disappears without being emphasized as a person.
- **Transition image:** first roofs of the hillside settlement appear below a single mast on the far side.

### 41. Settlement Streets / Mast Terrace — exact visual target

- **Composition:** roofs and doors sit at different vertical levels because the settlement is built into the hillside. A roof edge may be nearly level with another house's doorway.
- **Route:** steps turning between retaining walls, arches, cisterns, roof-water gutters, narrow alleys that repeatedly conceal the next street.
- **Foreground:** rough whitewashed wall corners, dark arches, wash lines, low parapets. Use real occlusion rather than fog.
- **Human traces:** freshly turned small plot beside dead vine, swept doorway beside leaf-drifted one, bowl weighted upside down, boot scraper with small mud heap, clothespins left on line.
- **Windows:** some lit/condensed, but reflections and channels through moisture prevent certainty about occupancy.
- **Tower:** darker, evenly cut stone above rough houses; mast and guy wires always provide orientation.
- **Mast terrace:** three lamps and cable returns visibly enter houses, including the first house the player knocked on.
- **Transition image:** tower door opens onto the worn hollow-centered stair.

### 42. Tower Traversal — exact visual target

- **Lower stair:** cable clipped along one wall, damp plaster shapes on the other, step centers physically worn lower by feet.
- **Hook landing:** seven hooks in a row and an eighth added at a different height, white chips/pale wall square below. Bench beneath them should allow a quiet pause.
- **Channel gallery:** narrow dark water separating two tower portions; service boat simple and small. The player's steadier handling is the visual point.
- **Older tower:** thicker walls, shoulder-width windows, repeated movement through narrow bright strips and dark stone.
- **Evidence:** radio card under brass clip by final stair; service record beneath it. Keep both physically mundane.
- **Scale:** after huge outdoor chapters, everything here is human-sized and close enough to touch.
- **Transition image:** the top room opens with almost no spectacle—just table, glass, paper, and brass arm.

### 43. Cabinet Choice / Original / Far Stair — exact visual target

- **Room:** modest rectangular chamber. One broad settlement window, no chairs, heavy bolted table, worn mat with two foot hollows. The lack of theatrical machinery is essential.
- **Cabinet:** laminated glass case, pen suspended just above paper, two brass joints, ink bead at nib, rear shutter exposing only the edge of sealed master plate.
- **Controls:** black and white handles share one slotted plate. Their mechanical interlock should be visible enough to understand that one face closes the other.
- **Glass history:** fingerprints low down, wiped patch at face height, reflections of protagonist/window moving across the pen and page.
- **Evidence floor:** all journey papers physically fan across boards—register, radio card, rations, ferry sheets, quarry notes, garden counts, drawings, dog. Leave walking gaps increasingly scarce.
- **Black-handle result:** settlement lamps go dark in groups; protagonist reflection vanishes from glass; master shutter slams and original lifts. Pantry lamp then produces a small warm-white island against otherwise dark room.
- **Original:** day-one seal crossing paper and metal, construction lines under ink, backward correction visibly part of the same old sheet.
- **Transition image:** released passage beyond cabinet is darker than the room and leads downward away from all visible signal wiring.

### 44. Unwired Street / Children's Shoes / Final Door — exact visual target

- **Yard:** narrow paving stones with grass between them, water barrel under broken spout, chair upside down to keep seat dry, blank wall toward tower. The scene should look almost aggressively ordinary.
- **Passage:** low opening into a street the player could not see from above. Put pantry lamp on ground briefly so its shadow makes the chair appear to move; then reveal the illusion plainly.
- **Street:** settled old paving with wandering gutter, low houses, one tree visible at a parapet gap, roofs hiding most of the tower behind.
- **Critical absence:** no signal wires cross this street. No lamps blink in patterns. No authored beacon points toward the objective.
- **Shoes:** two small shoes at one dark doorstep, laces loosened to the lowest eyelets, mud in seams, uppers wiped clean, damp rag beside the second shoe's recent print.
- **Door:** peeling paint at latch, loose knot in wood pressed back from inside, no note, symbol, socket, light, or clue marker.
- **Lighting:** pantry lamp illuminates shoes, coat hem, worn latch, and only the lowest part of the door. Above that, the door disappears naturally into darkness.
- **Final composition:** after the unpatterned knock, keep protagonist's loose hand near the wood and the two upright shoes inside the lamp pool. Do not pan upward, reveal a silhouette, pulse the exposure, or imply an answer. The only visible motion may be the small water drop into the barrel or the lamp flame settling.

---

## Global pacing curve

Most ordinary travel should sit at roughly **0.72–0.85× natural walking speed**. The game should not feel like a speed-run between narrative objects.

Use movement outside that band only when the story demands it:

- **0.50–0.65×:** intimate evidence, fatigue, shelter, grief, or deliberate physical manipulation.
- **0.68–0.78×:** careful traversal and most close environmental storytelling.
- **0.80–0.86×:** exposed travel across broad spaces.
- **>1.0×:** rare urgency only. The false answering light on the ridge is the primary case.

Heavy actions should visibly resist the character before inertia takes over: pushing a tree, rolling a stone, shifting a wagon, or operating a stiff mechanism should begin with effort, not immediate movement.

## Camera intensity arc

The camera should follow this overall emotional progression:

**Domestic stillness → organic forest sway → vertical instability in the city → broad quiet landscapes → monumental quarry/dam motion → exposed ridge → progressive stabilization → complete stillness at the final door.**

Level 5 sway (±5.8°) should be exceptional. Most major vistas only need level 3–4 because depth planes, parallax, foreground occlusion, and authored framing changes should create the sense of scale.

## Weather arc

Weather should occur in localized narrative blocks rather than functioning as a permanent level filter:

- Forest: fog, leaf fall, wet canopy.
- City: mist becoming heavy rain, then weakening.
- Oil fields: crosswind and grit.
- Railway: comparatively still and clear.
- Marsh: faint breeze and reflective cloud changes.
- Quarry: dust sheets and exposed gusts.
- Glasshouses: deliberately suppressed weather; warmth and living micro-motion replace it.
- Dam: spray escalating into the game's strongest storm.
- Ridge: snow and sleet that progressively close visibility.
- Settlement: damp stillness.
- Final street: only slight night dampness.

## Endgame effect removal

The **dam is the visual and environmental maximum**. From that point forward the game should progressively remove spectacle:

1. Dam: maximum water, weather, scale, and physical force.
2. Ridge: emptier country and less shelter.
3. Settlement: human-sized architecture.
4. Cabinet: almost no ambient movement.
5. Final street: no signal grammar.
6. Final door: ordinary physical evidence, an ordinary knock, and a static frame.

The final knock should therefore feel like the first action in the story that is not mediated by a signal, machine, copied hand, prepared route, or visual effect.