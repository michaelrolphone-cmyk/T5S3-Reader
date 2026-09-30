# Hollow Trail — Playable Scene Set Direction

This document converts the novella's visual sequence into **44 reusable playable scene sets**. A playable set may contain several narrative cuts when the same geometry and asset family can be reused with different framing, weather, foreground occlusion, lighting, or staging.

The direction is intended to preserve the story's environmental arc: **domestic stillness → organic forest → vertical city → exposed industrial country → quiet water → monumental quarry and dam → emptied high country → dense settlement → absolute stillness at the final door.**

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