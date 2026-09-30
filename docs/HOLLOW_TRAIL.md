# Hollow Trail 1.1.26 — walking with the character

Scene-directed walking pace, a lower fixed jump arc, slower climbing and
closer framing replace the running-jump rhythm. The forest alternates between
1.5x intimate views and broad vistas; two sparse near trunks establish an
observer moving beside the path. The character and environment scale together.
Shorter gaps and adjusted steps keep every chapter traversable at walking pace.
Existing living-tree placement and deliberate Up entry remain.

See [scene direction](HOLLOW_TRAIL_SCENE_DIRECTION.md) for authoring values,
movement behavior, route adjustments and validation. App **1.1.25 -> 1.1.26**;
minimum firmware remains **1.3.37**.

# Hollow Trail 1.1.25 — first forest revision

Level 1 restores the depth, monumental scale and directional light of PR #309
without restoring its added climbable-tree placements or its living-tree
renderer. Three continuous distant ridges move at 18/37/56 percent of camera
travel; two sparse background tree planes move at 30/55 percent. Pale openings
and narrow shafts separate those planes from the original foreground trees.
Distant tones use the existing 2x scenery sampling scale; trees, terrain and
actors keep full logical resolution. The original climbable-tree population
and Up-only entry from PR #310 remain. PR #311's camera comparison controls
are preserved.

The opening rock/pressure-plate/gantry assembly is removed. One dead snag at
the eroded bank can be pushed with the existing interaction button. It rotates
about its roots, accelerates through the fall and rests across the ravine.
Only the settled trunk is a solid crossing; its future location cannot catch
a falling player. A completed crossing survives a death, while restarting the
chapter resets it. An interrupted fall resets safely on respawn. There are
no forest traversal instruction overlays or automatic proximity activation.

The original loose stone now moves through a rounded depression in the ground.
Its own body can fill the hollow and provide footing, independently of the
trunk. No rock position moves any forest terrain. The remainder of the forest
retains its continuous rounded contour and existing rope crossing. Industrial
mechanisms in other chapters are outside this level-1 change.

The forest skips background strips that its opaque depth composition replaces,
including app warmup and speculative cache work. An unchanged camera/altitude/
vista reuses the background in the unused terrain cache plane; no framebuffer
allocation or increase to `HT_MEMORY`. All changing-frame raster loops retain
bounded cooperative checkpoints. Cache invalidation covers rebinding memory,
chapter changes, projection changes and scratch-plane use.

Version: **1.1.24 -> 1.1.25**; minimum firmware remains **1.3.37**.
Implementation checks and host timing are recorded in
[render performance](HOLLOW_TRAIL_RENDER_PERFORMANCE.md). Device FPS and
on-device visual acceptance are not claimed.

# Hollow Trail 1.1.8

Reworks the first ferry composition after visual review of 1.1.7 against the
boat reference. Water now paints continuously behind the entire marsh bank;
the physical channel bounds no longer clip the water at an eroded cliff edge.
Three cave masses overlap at different parallax rates around a lit opening.
Dark, sharp silhouette cores with narrow diffuse edges replace broad blur.
A distinct horizontal waterline, sparse ripples and a broken hull reflection
keep the water visible beneath the luminous opening. Water does not blend
away into the cave backdrop.
The skiff is a solid curved hull with a tapered mast and one continuous stay.
Removed decorative rigging, exposed ribs and the idle oar; rowing still follows
the seated player's stroke. The boat deck is now 38 pixels below its old
position, beneath the shore lip as in the reference. Boarding follows the
actual step down and the return uses a normal jump from the boat. The route
witness exercises those actions through normal physics. The irregular near
roof still shares its silhouette with head collision; hull width is unchanged.
The two background faces have independently authored shelves and fractures.
Their dark interiors fade toward the opening, while silhouette edges retain
sharp cores and narrow diffuse fringes. The circular player halo is suppressed
in this already-separated scene; shoreline vegetation is kept sparse.
The extra cave profile uses 960 bytes of static scratch, no new framebuffer,
and the bounded raster pass checkpoints every 16 rows.

Version: **1.1.7 -> 1.1.8**, minimum firmware unchanged at 1.3.37. Focused
ASan/UBSan route tests, water continuity/depth checks, cache/full-render
comparisons and the Xtensa app build pass. Shore and mid-crossing host renders
were inspected. Hardware appearance and FPS remain unmeasured.

# Hollow Trail 1.1.7

## Reference-led landscape and grotto ferry

Hollow Trail **1.1.6 → 1.1.7**, from master after PR #287 merged. Minimum
firmware remains **1.3.37**. The supplied forest and cave/boat reference images
informed the composition, terrain silhouettes, foliage grouping and scale;
the game renders its own procedural geometry and contains no copied image assets.

Compared the original 1.0.0 forest and 1.0.14 city renderers. Restored the
far forest hill/tree/mill layer and the tall far city skyline, which had been
bypassed by the newer distant-landscape dispatch. Their independent 0.22/0.53
parallax rates are preserved. Far silhouettes now retain contrast above the
sky after fog; other chapter ridges also receive stronger tonal separation.
The grotto deliberately keeps its pale open-water composition.

Trees use buried trunks and tessellated curved roots, with no vertical fill
pedestals. Static trees, landmarks and finds render before the soil, allowing
the actual terrain to mask their buried bases. Wide rigid scenery samples its
whole footprint; small chests and seedlings have their own soil contacts.
Player separation mist follows vista projection and cannot bleach the soil.

Exposed natural cliff faces now recede and project in unequal strata below
an intact walking lip. The same profile supplies drawing and player/loose-body
side contacts, including contact while falling past a projecting stratum.
Machinery sockets, adjoining land and ladder recesses retain their geometry.
Natural ground has clustered fine grasses, fern fronds, half-buried faceted
rocks and shrubs behind the path. Tree foliage groups at branch ends, branch
origins follow the actual bent trunk, and buttress roots curve into sampled
soil. The forest ravine rope hangs from a large rooted tree limb.

The marsh's first ferry crossing is composed as a rock grotto: a heavy dark
roof, sparse uneven bank, pale opening, low wooden skiff, mast/furled cloth,
slack rigging, shore ring/chain and faint strip reflections. Sunken shelters
and willows begin beyond this opening. The roof uses the same authored curve
for head collision and rasterization. A smooth distance ramp shades only the
roof/fog region, reusing terrain-column scratch rather than allocating a blur
surface. Open-water brightness eases into the normal marsh ambience inland.
The ferry hull is 96 logical pixels wide; boarding, standing support and
travel stops use its actual half-width. Other chapter boats keep their prior
60-pixel width. All boat geometry follows the same moving boat snapshot.

Validation: full native suite, focused ASan/UBSan gameplay checks and rendered
scene/cache comparisons cover the changes. All ten chapters, all 30 finds and
both endings remain covered. Added checks verify eroded cliff silhouette and
side contact, loose bodies avoiding premature wall snapping, grotto ceiling
contact and full hull containment at both banks. Hardware visual quality and
FPS remain unmeasured.

# Earlier update: Hollow Trail 1.1.6

## Continuous landscape contours and grounded animation

Hollow Trail **1.1.5 → 1.1.6** follows merged PR #285; minimum firmware
remains **1.3.37**. Natural ground now follows authored world-space contour
points rather than a symmetric bump that resets at every collision parcel.
Joined forest, sand, mud and planting beds share continuous slopes; quarry
and pass profiles use angular rock contours. Roofs, working pads, bridges,
rope takeoffs, water crossings and puzzle machinery retain their functional
heights. Rendering, walking, loose bodies, evidence and weather still consume
the same surface function.

Foreground trees no longer use flat root endpoints. The trunk footprint and
both buttress roots sample the local soil, filling the previously visible
triangular air gaps on hillsides. Contacts use the same camera and vista
projection as the ground. The close scenery compositor also no longer cuts
its output at a fixed 90 logical pixels.

Pulling a crate or boulder leans the torso away from the load, steps backward
and extends the arms while keeping both hands on the load. Pushing keeps its
forward lean and bent-arm brace. Signed load motion selects the moving pose;
the last intended direction selects the pose at rest or when blocked.

Regression coverage includes continuous parcel joins, isolated tree-to-soil
contact on both slope directions at three vista scales, distinct mirrored
push/pull silhouettes with unchanged hand contacts, and the existing ten-chapter
input-driven route. Full native tests, focused ASan/UBSan gameplay checks and the official Xtensa
ELF/import build passed. The source manifest, ELF sidecar and generated app
catalog agree on 1.1.6. Hardware appearance and frame rate remain unverified.

# Earlier update: Hollow Trail 1.1.5

## Discovery-driven mechanical deduction

Hollow Trail **1.1.4 → 1.1.5**, from master after PR #284 merged. Minimum
firmware remains **1.3.37**. See [the current puzzle design](HOLLOW_TRAIL_PUZZLES.md)
for the evidence chains, deductions, operating rules and recovery guarantees.

This replaces the shallow rail brake sequence with a three-wagon shunting
problem, the single lock with a two-chamber finite-water problem, and independent
hoist dials with a conserved stock of weights. Garden venting and dam priming
introduce physical prerequisites that must be understood before the final
configuration can work. The ridge requires diagnosing and isolating a returning
echo. The city now distinguishes a correct endpoint route from a safe one.

Operating facts are distributed across different finds. The mill's three socket
rubbings reconstruct its plated-over frame; later documents contribute distinct
capacity, direction, conservation and phase constraints. Remove the automatic
answer-like narration and chapter recap hints. Feedback describes what the
machine did; RUN tests the player's hypothesis instead of silently detecting
an intermediate correct setting. Wrong experiments retain useful progress and
remain recoverable. Existing camera, scene weather, animation and themed maps
are retained.

## Grounded terrain, scale and rope contact

The ten chapters no longer render their middle sections as identical 18px
floating shelves. A shared integer surface profile supplies drawing, feet,
loose-body support, discovery interaction, landmarks, checkpoints and weather
occlusion. Walking follows both sides of hills without requiring jumps or
letting the character sink through a rising surface. Thick terrain/buildings
have physical side walls; ladder approaches use matching visible recesses.
Raised movable bridges remain solid and retain their mass/plate mechanisms.

| Chapter | Walkable environment |
| --- | --- |
| Forest | Continuous rounded, rooted earth; a mill span and two purposeful ravines; enormous trunks and overhanging limbs |
| City | Building bodies, rooftops, service connections and recessed ladder approaches |
| Oil fields | Low eroded ground, service trenches and one supported catwalk |
| Rail yard | Ballast and embankments, an abandoned locomotive and two carriages on a parallel track, a supported broken trestle and signal gantry |
| Marsh | Low mud hummocks and reed banks around an actual ferry channel |
| Quarry | Solid cut faces, uneven stone benches, hoist and ladder recesses |
| Gardens | Overgrown beds and retained masonry terraces among greenhouse frames |
| Dam | Masonry service routes beside a large reservoir wall and four cascading spill sheets |
| High pass | Snow-edged rock slopes, cliff faces and exposed ravines |
| Last light | Hillside buildings and substantial tower floors with visible walls |

Remove all hovering clue boards. The first forest discovery now lies on the
initial forest floor: cloth caught in bark. Other finds rest on or attach to
scene objects. Collected prose retains every operating fact required by the
new puzzles. Gameplay captions and nearby prompts use subdued gray; the
apparatus prompt appears only within reach of a control. No distant objective
flags or automatic solution hints are introduced.

Authored wide-shot envelopes at selected clearings, towers, crossings and
cliff/dam reveals ease the sharp world projection from 1x down to **11/16x**,
with chapter-specific upward framing. A 480px view can expose about 698 world
pixels at maximum width. Foreground terrain, character, props and landmarks
share the projection; distant cached scenery remains a stable far plane.
Normal 1–1.2x breathing, mood-driven rotation and the fall zoom envelope remain
unchanged and compose with this framing. The vista envelope advances only with
movement. Input, collision and rope positions always retain world bearings.
There is no larger framebuffer or additional full-screen resampling pass.

A/Confirm can catch any reachable rope segment. Grip distance is stored along
the constrained rope, not snapped to its tail. Gravity projected along the
local segment accelerates a bounded, gradual slide; pumping acts at the held
segment. The player can release or jump before reaching the end, retaining the
local swing/slide velocity. Existing rope length constraints and slow momentum
build-up remain in effect.

Development checks: the full native suite and final Xtensa ELF/import build
passed. Focused ASan/UBSan checks cover all ten input-driven routes, thirty
finds with zero deaths, puzzle-state recovery, slope/raster agreement, six rope
grab heights and mid-rope jumps. Render snapshots, cache equivalence, controls,
endings and pipeline ownership checks passed. Host previews were inspected
across all chapters, including the wide dam view. Batched solid-terrain fills
keep the sampled warm-cache host render time close to the preceding renderer;
this does not establish on-device 24 FPS.

Source manifest, ELF sidecar and aggregate catalog agree on **1.1.5**:
**149544 bytes**, SHA-256 `92e059bf9fd1a0e22218b1d6ff0a01db20fe685c6c03e7c6f4813a7abec06081`. These are software checks, not hardware
display/FPS qualification.

# Earlier update: Hollow Trail 1.1.4

## Scene direction, mechanisms and physical performance

Hollow Trail **1.1.3 → 1.1.4**, directly from master after PR #282 merged.
Minimum firmware remains **1.3.37**. The sections below this update retain
historical implementation notes; the rules here supersede older puzzle,
weather, camera, map and fall-timer behavior.

### Story scenes, not chapter-wide weather

A discovery starts a cue; a specified later discovery ends it. Walking into
an exposed area without that discovery does not create weather. Within an
active scene, terrain supplies shelter and a 160px boundary fade. The cue
itself builds over 2.048 seconds and clears over 1.024 seconds. Revisiting the
first document after the closing discovery cannot restart it. Death retains
scene progress; a fresh chapter/debug replay resets the scene while retaining
the journal archive. Reading and pause freeze the simulation clock.

| Chapter | Starts after | Ends after | Visual direction |
| --- | --- | --- | --- |
| Forest | Thread in the Bark | Mill Register | Leaves and a dimmer clearing: guidance becoming admission |
| City | Burnt Fuse | Watch Log | Rain, darkened sky and wind after the extinguished way out |
| Oil Fields | Lantern Tag | Stove Plate | Grit and muted light around the cost of the gift |
| Rail Yard | — | — | Still air around the cancelled journey |
| Marsh | — | — | Still water around the prolonged wait |
| Quarry | Brake Notice | Torn Note | Windblown chalk and dimming around responsibility for the load |
| Gardens | — | — | Sheltered stillness: safety and confinement |
| Dam | Garden Pipe | Ridge Circuit | Dark sky, rain and gusts after the common supply revelation |
| High Pass | Other Half of the Scarf | Wind Shelter | Sleet and reduced light around uncertain guidance |
| Last Light | — | — | Stillness around the final decision |

The dam cue has two brief lightning pulses, 5.12 and 12.16 seconds after the
cue begins, only while it remains active and the player is exposed. Each lasts
at most 256ms with decay. Sky illumination reuses the existing scene clear and
max compositor, preserving foreground silhouettes and upright UI. There is no
endless flashing loop, display waveform change or extra full-screen pass.

### Camera mood and falls

The existing **1–1.20× breathing zoom** and its 13.1-second movement cycle are
retained. Rotation has its own Q8 phase. At minimum tension, its amplitude and
speed are approximately one-sixth of 1.1.3: roughly **±0.8–0.96° over 39 seconds**.
Authored continuous chapter curves plus active discovery cues can raise both
to the former **±5.76° / 6.55-second** range, with eased transitions rather
than abrupt intensity changes. The dam revelation can reach that upper bound.
Normal camera phase advances only during actual player-directed movement.

Dropping more than 40px below the last supported height widens toward the
uncropped 1× view over about half a second; recovery eases back into the
existing breathing phase over about two seconds. The corner-safe rotation
crop remains the lower limit on zoom. Ordinary jumps do not trigger it.
This drop envelope may finish while stationary; normal sway still freezes.

**The one-second free-fall death timer is removed.** Falling beyond the map's
bottom (world y > 500) causes respawn. Long legitimate descents survive. All
camera effects are visual; coordinates, collision and input bearings are fixed.

### Mechanisms that expose their rules

Eight chapters replace recipe-style switches/dials with causal mechanisms.
The final familiar signal remains a narrative recall before the cabinet choice.
Evidence prose and compact entries no longer give obsolete switch recipes;
physical traces beside finds, machine diagrams, and notebook observations
explain operating rules. Errors do not destroy resources or permanently jam a
mechanism. Completion latches, survives death, and visibly opens the boundary
over 48 simulation steps before passage is allowed.

| Chapter | Reasoning and feedback | Boundary into the next setting |
| --- | --- | --- |
| Forest | Clutched adjacent shafts turn together; align cams with worn slots | Timber mill loading door, city brickwork beyond |
| City | Trace three feeds through pair-swapping contacts; reverse outer returns while retaining heat | Riveted service hatch, oil pipes beyond |
| Oil Fields | Divide eight measures equally using 8/5/3 vessels; taps transfer until full or empty | Round tank bulkhead, railway beyond |
| Rail Yard | Brake, shunt the wagon clear, change points, then dispatch | Lifting rail barrier and signals, marsh reeds beyond |
| Marsh | Seal a lock, equalize pressure, open its upper gate | Two lock leaves and a cut-stone channel |
| Quarry | Allocate six loads; balance 3:2 lever arms and leave weight on the brake | Hoist cage, conservatory glazing beyond |
| Gardens | Trace a reflected beam through shutter/mirror/clear positions to the low receiver | Glass conservatory lattice, dam conduits beyond |
| Dam | Redistribute a conserved six-unit supply without starving turbine, heat or ridge | Heavy sluice and masonry, exposed rock beyond |
| High Pass | Read four shelter-post heights and translate them into chimes | Snow-covered shelter gate, tower lines beyond |
| Last Light | Recall the established three-flash signal, then make the existing cabinet decision | Interlocked original/live-pen cabinet |

Pipes return visibly to their source, contact-bank lines show their current
routes, vessels display contents/capacity, a balance tilts with torque, and
an optical bench uses the same bounded ray rules as its receiver. No per-frame
allocation, unbounded search, or new full-screen rendering pass is introduced.

### Routes and character contact

| Setting | Route emphasis |
| --- | --- |
| Forest | Ground-level floor and pits, a weighted bridge and one vine crossing; no ladders or crate staircase |
| City | Rooftops, ladders, service access and a crate-operated span |
| Oil Fields | Ground-level machinery and a boulder-filled trench, one elevated service catwalk |
| Rail Yard | Mostly track grade, one broken cutting/vine crossing and one signal-gantry climb |
| Marsh | Water-level banks and boardwalks with a boat crossing; no ladders |
| Quarry | Vertical working terraces and lifting infrastructure |
| Gardens | Planting floors, a crate-assisted ledge and one upper maintenance walk |
| Dam | Reservoir crossing followed by ascent through machinery |
| High Pass | Cliffs, exposed climbing and a rope crossing |
| Last Light | Service approach, water crossing and signal-tower ascent |

Ladder poses alternate hands and feet relative to actual rung heights, freezing
when climbing stops. Ledge poses retain contact with the lip, bend/extend elbows,
then bring a knee and planted foot onto the platform. Pushing uses both hands
against the actual crate face or boulder rim, a leaning torso and bent legs;
foot motion follows load movement, and a blocked load produces a planted brace.
The player silhouette remains connected throughout each pose.

### Development checks

The full native suite passed, including controls, both cabinet outcomes,
render-time simulation, pipeline ownership and cached/reference rendering.
The final source also passed focused ASan/UBSan and the official Xtensa ELF/
import build. All ten input-driven chapter routes reach their mechanisms,
collect all 30 documents and complete with no deaths. Additional checks cover
conserved fuel/power, pressure/interlock rejection, optical reception, weather
start/end/replay semantics, lightning pulses, baseline/peak camera coefficients,
unchanged breathing range, drop recovery and map-bottom-only fall death.
Host previews of mechanisms, gates, weather and contact poses were inspected.

ELF/sidecar/catalog agree on 1.1.4, **139280 bytes**, SHA-256
`61b9fdf54bce579d27d56d1266c6f9e854f759a6251d148d5627cba2e2edca87`.
A warm-cache moving-focus host sample measured 0.736ms render+pack. This is a
host development sample, not device FPS or a controlled comparison of the
changed scenes. The 24 FPS target remains unverified on the S3/display.

# Historical implementation notes (through 1.1.3)

## Atmospheric direction and interaction polish (1.1.3)

Hollow Trail **1.1.2 → 1.1.3**; minimum firmware remains **1.3.37**.

The original visual transform was active but only ±0.6° with 1.3% zoom change.
It now reaches roughly **±5.76°** and **1–1.20× zoom**, with a 6.55-second sway
cycle and a 13.1-second breathing cycle while moving. Phase advances only when
movement input actually moves the character (including a committed climb), and
freezes at rest/reading/pause. World collision and control bearings are unchanged;
text and UI remain upright. Q12 matrix coefficients use a 344/4096 maximum
off-diagonal term; a rotation-dependent inward crop covers the viewport
corners, combined with a 0–682/4096 breathing reduction. This adds only a
few integer operations per frame, with no trigonometry or extra raster pass.
All four bilinear taps remain in bounds over the
complete phase cycle, so the existing clamp-free pass is retained.

Weather is spatial story direction rather than a global overlay. Entering or
leaving an exposed interval fades particle count and physical wind over 160
world pixels. Returning to shelter removes them again. Ground and overhead
platforms occlude particles using the physical terrain segments.

| Chapter | Weather / atmospheric intent |
| --- | --- |
| Forest | Dry fog; a few leaves in the clearing from x850–1750 |
| City | Light rain and wind across exposed roofs, x500–1200 |
| Oil Fields | Grit across the machinery, x1250–2250; no rain |
| Rail Yard | Still, silent air |
| Drowned Marsh | Still water and reeds; no rain or wind |
| White Quarry | Windblown chalk, x650–2100 |
| Glass Gardens | Shelter and tended growth; no weather |
| Broken Dam | Heavy rain and gusts, x500–2620; hardship at the crossing |
| High Pass | Wind-driven sleet, x180–2650, easing before the exit |
| Last Light | Stillness around the final decision |

There are two inspectable environmental scenes per chapter. **A/Confirm** near
one reveals a short close observation for about seven seconds without opening
the journal. Evidence, props and puzzle actions keep priority. Stitched trail
cloth, crossed-out arrows, a locked service door, fuel allowances, a cancelled
ticket, paired ferry records, a repaired hoist grip, an inside garden latch,
return pipes, guyed signal towers and a mechanical writing arm connect the
physical route to the two competing accounts. Observations point to material
details rather than deciding either witness's motive.

The three-flash lamp motif repeats across the route. In the last chapter its
pen arm shares the signal circuit; breaking the feed stops both, and examining
it then describes the stopped mechanism. The final decision/evidence logic is
unchanged. Foreground surfaces now use chapter materials: metal seams/rivets,
rail sleepers, weathered dock planks/stilts, fractured stone and garden masonry.

Interaction feedback follows real motion: the weight plate's cable leads to a
pulley and latching bridge, boat wakes and oar strokes require actual velocity,
crates scrape as they move, and a landing briefly compresses the upper body and
kicks up a bounded contact plume while feet remain on their collider. Rendering
uses a frozen game snapshot. No allocations or full-frame passes were added;
particle and scene counts remain fixed and clipped to the visible route.

Validation includes all ten input-only routes and clues, HID/XInput inspection
without opening the journal, weather window/shelter/occlusion checks, full-cycle
transform source bounds, immutable world coordinates, all observation text
widths, deterministic rendering and ASan/UBSan. Host previews of all ten chapter
landmarks were inspected. The full native suite and official Xtensa ELF/import build pass.
Matched -Os moving-focus host samples measured 0.783ms for 1.1.2 and 0.828ms
for the initial atmosphere pass. The final boat/depth/bridge/rope/camera
follow-up measured 0.809ms (about 3% above the 1.1.2 sample, subject to normal
host timing noise). Actual S3 frame rate and physical display feel are not established
by these host checks; 24 FPS remains the target.

### Follow-up interaction and depth corrections (same unreleased 1.1.3)

- Up/Down engages a ladder on grounded or airborne overlap, including contact
  reached during the current physics step. No vertical input means pass through.
  Down at the foot and Up at the top do not capture; Down at the top descends.
  Generic navigation selects ladder intent nearby instead of jump/pause. B has
  a short recapture cooldown. A is not required.
- Boarding seats the player at the centre thwart. The seated knees, torso and
  hands share the oar's stroke; walking stride is not rendered while rowing.
  Boat limits include the full 30px half-width, keeping the hull between actual
  water boundaries. Three channel endpoints and disembarkation witnesses were
  adjusted accordingly; a stopped boat cannot continue into either bank.
- Far scenery now has its own chapter composition: forest canopy/ridges,
  skyline clusters, mesas, railway cuttings, island banks, quarry escarpments,
  conservatory domes, reservoir valley/aqueduct, snow ranges and a distant
  settlement. Middle-distance structures remain separate, and close framing
  uses chapter-specific limbs, fire escapes, pipes, gantries, willow fronds,
  conveyors/chains, broken glazing, conduit, overhangs and insulated wires.
  These are different geometry, scale, spacing, seeds and tones, retained in
  the existing cached parallax planes (0.22×, 0.53× and 1.23× translation).
- Weight bridges park 224px above their lowered position, beyond a normal
  jump. Their visible top is their physical top at every lift position, with
  solid landing/head/side collision. A standing passenger rides down with the
  bridge. Evidence and decorations ride at the same actual height; reaching
  a raised span by another route is valid. Lowering is not a permission flag.
- Rope pumping force drops from 384 to 64 Q8 units, with bounded inertial
  increments. Holding one direction gives a modest first swing (~32px in the
  host witness); timing inputs with the return swing builds ~50px of reach.
  B gives the normal jump impulse and retains horizontal swing velocity.
  The route witness now pumps across successive swings before releasing.

The all-chapter route, explicit hull/seat bounds, raised-bridge landing/riding,
held-versus-timed rope pumping, directional ladder cases, cached/fresh scenery
comparison and full camera-phase source bounds are exercised by native tests.

## Physical props and distinct routes (1.1.2)

Hollow Trail **1.1.1 → 1.1.2**, minimum firmware unchanged at **1.3.37**.

The stone is now 32 logical pixels across beside a 29-pixel character, with
facets, rotating fractures and a circular standing surface shared with its
collider. Pushing accelerates it gradually; release preserves rolling momentum
with low friction. The 24×26 crate has planks, side shading, braces and nails,
accelerates under effort and stops quickly through higher friction. Both obey
gravity, terrace contacts and side walls. Supported feet follow moving props.
Stone sockets are cut from the same terrain segments used by drawing and
collision. A weighted plate lowers and latches a suspended bridge over 32 ticks.

Boats have tapered hulls, raised gunwales, seats, ribs and moving oars, with
inertia, water drag and bounded bobbing. The deck stays at or above shore level
for reliable disembarking. Ropes hang 84 pixels from visible supported anchors;
seven Verlet nodes use gravity, inertia, directional pumping and six bounded
constraint passes, rather than an authored endpoint arc. Catching preserves
the hand position, player/deck collision remains active, and release carries
endpoint velocity. A jump-off uses the normal jump impulse and retains horizontal swing momentum.

Ladders engage with **Up/Down while overlapping**, including in midair.
Walking or jumping through without vertical input does not attach. Down at the
bottom and Up at the top leave the player free; Down at the top descends. B
jumps off with a brief recapture cooldown. A is not needed. A continuous downward free fall dies at the first 32ms physics
tick beyond one second (1024ms). Ground, a caught ledge or an attached climb/
rope ends that fall. Releasing jump still shortens the jump as before.

| Chapter | Mechanical route |
| --- | --- |
| Forest | Weight bridge, crate-assisted ledge, canopy ladder, rope ravine |
| City | Two-stage rooftop climb, crate weight bridge, return roof ladder |
| Oil Field | Raised gantry, stone socket bridge, crate-assisted upper shelf |
| Rail Yard | Switchback tower ladders, overhead rope crossing, final climb |
| Village | Lake crossing first, then rooftops and descent through the village |
| Salt Flats | Stone into a recessed socket, bridge, alternating high/low shelves |
| Gardens | Move a crate to reach the first ledge, then two garden climbs |
| Dam | Service ladders, reservoir boat, upper spillway rope gap |
| High Pass | Immediate rope ravine, ridge ladder, movable crate ascent |
| Last Light | Crate weight bridge, double ascent, descent to a final boat crossing |

The ten maps have separately authored spans, elevations, overlap and prop
placements. They no longer run every prop in the same order. Existing chapter
story puzzles and evidence remain, and pause-menu selection reaches every map.

Validation: an input-only journey completes all ten maps, all 30 evidence
records and the ending without deaths. Focused checks cover circular feet,
rolling inertia, crate friction, directional ladder capture, rope length/floor
constraints and the 992ms/1024ms death boundary. Host art previews were inspected. The full native-app suite, focused ASan/UBSan
checks and the official Xtensa ELF/import build pass. The warm-cache moving-focus
host benchmark measured 0.731ms render+pack; this is not an S3 frame-rate result.
Physics has fixed object/node counts, no per-frame allocation and no new raster
pass. The 24 FPS target and prior renderer optimizations remain; device timing
and physical controller/display feel still require measurement.

## Current controls (1.1.2, supersedes historical mappings below)

| Action | Controller | Device / generic navigation |
| --- | --- | --- |
| Move / turn journal page | D-pad Left/Right | Left/Right |
| Jump / leave rope or boat | B | Up when not attached |
| Engage / climb ladder | Up/Down while overlapping | Up/Down while overlapping |
| Pull up from ledge | Up or A | Up or Confirm |
| Drop from ledge | Down (B jumps away) | Down |
| Grab / release object; row or pump swing | A, then Left/Right | Confirm, then Left/Right |
| Inspect / use nearby mechanism | A | Confirm |
| Journal | Start (toggles reading) | Down to pause, then Confirm |
| Read / confirm decision | A | Confirm |
| Back in reading | X | Back |
| Close app from gameplay | Home / menu shortcut | Home / Back |
| Pause / resume diagnostics | Select | Down |
| Toggle DSP benchmark while paused | B | Up |

A and X never close gameplay. A duplicate mapped Back accompanying a raw face
button is suppressed. Device Back remains available with a neutral controller;
the host Home/menu exit request remains active in every game screen.

A away from an inspectable object or mechanism is inert. The journal never
opens as an inspect fallback. Directional inputs cannot directly open it.
Start has a dedicated edge-triggered journal action, separate from jumping,
inspection and pause. Holding Back after closing reading does not exit the app.

The owner found A/B reversed on the actual receiver in 1.0.15. Version 1.0.16
corrects Hollow Trail's face-label bindings (without changing driver encoding):
XInput A/B/X/Y/Start/Select = 0x01/0x02/0x08/0x04/0x200/0x100;
HID = 0x02/0x01/0x04/0x08/0x80/0x40. The app declares and leases both optional
`usb.xinput.gamepad` and `usb.hid.gamepad` capabilities; raw HID reports are not
interpreted as normalized XInput. One connected pad supplies a frame, with
XInput priority; multiple receiver slots are never ORed together. A raw poll
failure suppresses mapped navigation while that pad owns input. A persistent
failure yields back to neutral-gated device input after 250ms. Source changes
and recovery require neutral input before accepting new actions. These rules
prevent a fault from turning a held button into a new inspect/journal press.

Regression tests drive production input with both raw layouts, duplicate OS
navigation, all eight hats, unused face buttons, XInput trigger bits,
Start/Select, empty-space inspection, reading/back, faults, recovery and a
second receiver slot. The cumulative app version is unreleased 1.0.16
(master/published 1.0.15). Firmware 1.3.36 contains the reader glyph fix and
optional no-wait input service.


## 24 FPS performance work (same unreleased 1.1.1)

The objective remains a complete render/pack frame inside the display's
41.7ms budget. The weather/debug PR now includes production hot-path changes,
not just an explanation of the previous device counters:

- Draw the world directly into the sway source buffer. Remove the per-frame
  129,600-byte copy (259,200 bytes of read/write traffic). Transform only the
  visible interior; the vignette owns the discarded pixels. Prove the four
  bilinear taps stay in bounds for every phase of the 2,048-step cycle.
- Rewrite bilinear arithmetic using differences, preserving rounding exactly.
  Remove per-pixel clamps and repeated edge checks inside that proven region.
- Cache two exact radial-lighting maps, shared across scenery depths. Reuse
  them for settled focus and one-pixel camera-follow alternation; rebuild on
  other focus changes with cooperative checkpoints. This adds 129,600 bytes
  of PSRAM, allocated once with the scene. The optional DSP path retains its
  original arithmetic and does not build an unused map.
- Pack four output bytes at a time using aligned word writes and source-word
  lookahead. A 2KiB dither table handles constant neighborhoods without
  interpolation. Unaligned output retains the byte-safe path. Pixel phase,
  dithering thresholds and interpolation results are unchanged.
- Compile only compositor, upscaler, affine sampler and mono packer for speed;
  the app-wide size optimization and bounded polling/yield behavior remain.

A reproducible harness is `test/native_apps/hollow_trail_benchmark.c`.
For this change, compare with engine revision
`2fd95b1372c6752f2f24e74db66eeddd30f8420a`, using the same app include files
(the only production change in this follow-up is the engine):

```sh
git show 2fd95b1372c6752f2f24e74db66eeddd30f8420a:Apps/hollow_trail_engine.inc > /tmp/ht-baseline.inc
cc -std=c11 -Os -IApps -Ilib/NativeApps/include -DHT_BENCH_ENGINE='"/tmp/ht-baseline.inc"' test/native_apps/hollow_trail_benchmark.c -o /tmp/ht-before
cc -std=c11 -Os -IApps -Ilib/NativeApps/include test/native_apps/hollow_trail_benchmark.c -o /tmp/ht-after
/tmp/ht-before --moving
/tmp/ht-after --moving
/tmp/ht-before --hashes > /tmp/ht-before.hash
/tmp/ht-after --hashes > /tmp/ht-after.hash
cmp /tmp/ht-before.hash /tmp/ht-after.hash
```

Host `-Os` measurements over 500 warmed frames across ten chapters:
changing focus **1.106 → 0.762ms/frame** (31% less); fixed focus with animated
sway **1.085 → 0.693ms/frame** (36% less). With `-O2`, the corresponding runs
were 1.133 → 0.724ms and 1.084 → 0.713ms. Packed output hashes match on all
170 chapter/phase cases. These are CPU comparisons, not ESP32/PSRAM/display
measurements or a claim that 24 FPS has been achieved. The next device run
must establish whether RENDER+PACK fits the actual budget.

## Debug level selection and weather (1.1.1)

Hollow Trail **1.1.0 → 1.1.1**; firmware minimum stays **1.3.37**.
From pause, Left/Right browses all ten chapters, wrapping at either end.
A/Confirm loads the selected chapter, X cancels selection (generic Back also
cancels). Without an active level selection, A still opens the journal;
Start always opens it. Select resumes and B still toggles the DSP experiment.
The destination starts with fresh puzzles, objects and checkpoint, preserving
collected evidence and witnessed endings. No later clues are granted merely
by selecting a chapter. Loading discards prepared old-scene frames, uses the
existing cooperative cache warmup and requires neutral input before gameplay.
Selecting the current chapter restarts it too.

Historically, 1.1.1 drew 48 rain streaks and 12 leaves everywhere. This is
superseded by the authored 1.1.3 weather windows above. That implementation used no
new allocation or full-screen raster pass. Deterministic particles share the
render snapshot; rain slants and leaves drift with a smooth reversing gust.
Weather freezes in pause/journal. Wind ramps in on spawning and nudges free
walking/falling by at most 40/256 logical pixels per 32ms tick (4.9 pixels/s,
versus 78.1 pixels/s walking). Collision resolves the wind displacement, so
idle gusts cannot push through a wall. Attached ladders, ropes, boats, ledges
and grabbed objects remain stable. Gust-only drift does not advance visual
camera sway. The existing 24-submission cap remains unchanged.

Owner's 1.0.16 device measurement: **13.4 FPS**, 24 scans/s; RENDER 50ms,
PACK 23ms, WAIT/CACHE/COPY 0ms, INPUT 9ms; SCAN 19ms, PREP 12ms,
DMA 2ms, PACE 23ms, ROWS 481. Render + pack is 73ms (about 13.7 FPS),
consistent with the measured submission rate. INPUT overlaps those stages;
scan preparation, DMA and pacing are not additive app-frame costs. A 24 FPS
producer needs about 41.7ms/frame, so roughly 31ms must still come out of the
measured render/pack path. These readings predate 1.1.0 traversal/sway and this
weather update; no new on-device speedup or particle cost is claimed.

Validation covers all ten debug destinations, selection-edge/held-button
behavior, cancellation, fresh-spawn state and discovery preservation. The
input-only journey still completes all ten chapters and gathers every clue
with gusts enabled. Positive/negative wind, displacement bounds, deterministic
weather, render snapshot equivalence and existing controller/journal checks
are covered by the native suite. Target ELF/sidecar/catalog use 1.1.1.

## Traversal, journal and visual camera (1.1.0 / firmware 1.3.37)

This update changes Hollow Trail **1.0.16 → 1.1.0** and firmware
**1.3.36 → 1.3.37**. The app requires 1.3.37 because detached typography
must report the dimensions of its target bitmap rather than rotating those
logical extents a second time. The input scheduling and face-button fixes
from 1.0.16 remain intact. Historical implementation notes below describe
earlier layouts and are superseded by this section.

The original 1.1.0 layout repeated the same mechanism sequence in every chapter.
Version 1.1.2 replaces that layout and its scripted props; see the current
route table above. Death preserves evidence, story, puzzle and opened bridge
progress and resets movable props to their authored starting positions.

An airborne approach within hand reach of a platform corner catches the edge.
It holds until Up/A climbs over it, Down drops, or B jumps away. Climbing is a
16-tick pull-up; dropping has a short re-grab cooldown. Holding horizontal
movement cannot automatically pull the player up.

The original 1.1.0 transform used ±0.6 degree of rocking and 1–1.013×
breathing zoom; 1.1.3 increases its amplitude and cycle speed as described above. The phase freezes when movement input stops,
including reading and pause. Bilinear fixed-point sampling transforms the
world image and player together before the vignette; input axes and collision
coordinates never rotate or scale. Journal, prompts and narration stay fixed.
The transform reuses existing scratch memory with cooperative checkpoints.
The obsolete terrain-cache plane is no longer generated or composited.
Hardware frame cost remains to be measured; this is not a 24 FPS claim.

The journal uses a stitched, worn notebook frame, chapter stamp and ink sketch,
with a clear 768×456 typeset area inside the landscape screen. Texture stays
outside text. Its index shows four discovered titles and an explicit selection
marker; arrows select, A reads, X returns. Body pages retain reader-selected
fonts, forward/back pagination and the ending's displayed-page confirmation
checks. If typography fails or returns no ink, the same frame contains a
compact, paginated text fallback. Large-font index/decision pages also fall
back if all choices cannot fit on one page.

Verification: the input-only route witness uses all five mechanisms across
ten chapters, collects all thirty evidence records, solves the authored
puzzles and completes a journey without a death. Targeted checks cover both
ledge approaches, deliberate climbing, drop cooldown, idle sway, independent
cache/render equivalence, input ownership, journal pagination and blank-font
fallback. The detached-renderer regression exercises its real constructor and
logical dimensions as well as glyph resolution. `scripts/build_all_apps.py
--id hollow_trail` builds and validates the target ELF and runs the native suite.


A separate, original ten-chapter silhouette platformer for the fast EPD interface, using fixed monochrome dithering by default.
No external assets, file access, or network are required. Version 1.0.2 uses
the native math API introduced in firmware 1.3.27.
The Limbo reference informs the monochrome forest/depth treatment; geometry and
character are generated by this app.

## Playing

- D-pad or left stick: move. Xbox A: jump (hold for a longer jump).
- Device/navigation Left/Right: move; Confirm: jump; Up: interact.
- Controller B: interact with nearby puzzle machinery. Xbox A still jumps.
- Start / Down: pause and resume. Select / Back: exit.
- While paused, A / Confirm (or Up) toggles the experimental DSP16 compositor.
  The pause panel shows ON/OFF (or UNAVAILABLE); display output always uses dots.
  The choice lasts until exit; launch defaults to dots. See [display modes](GAME_DISPLAY_MODES.md).
- Cross the forest’s nine pits to its ruined doorway to enter **Skyscrapers**.
  The second level crosses ten rooftops at varying heights, above a foggy skyline.
  Reach its antenna doorway to enter **Oil Fields**, crossing desert ledges among
  cactus, derricks and pumpjacks. Continue through seven more settings to find
  your sister at The Last Light; finishing chapter ten loops back to the forest.
- Falling returns to the most recent checkpoint (ledges 4, 7, and 10).
- Short coyote time and jump buffering make the takeoffs forgiving.

## Rendering and performance contract

- Native 960×540 landscape, 1bpp video by default, firmware minimum 1.3.33; output uses monochrome dots.
- White far plane; two scenery planes at 0.22× and 0.53× camera speed with hills,
  trees, abandoned mills and towers; black playable silhouettes at 1×;
  closer framing branches at 1.23×.
- Depth blur radii 3/1/0 logical pixels and additional outer-focus radii 9/6/4.
  Smooth radial mixing/fading is centered on the character. Character and UI
  are composited afterward to preserve their legibility. Local mist behind the
  character keeps its silhouette visible when crossing a black tree trunk.
- Default: fixed 8×8 ordered dots at physical-pixel resolution represent the
  full composited fog/blur image using black and white endpoints. Native gray
  mode retains four physical shades and dispersed interpolation between them.
  Neither mode uses temporal dithering or randomized per-frame noise.
- Working PSRAM is **3,029,940 bytes**, plus up to 15 bytes of alignment.
  Sixteen fixed world strips cache near/wide scenery for four independent
  parallax planes. Cache storage is bounded, with no per-frame allocation or
  internal-RAM fallback. See the layout and scheduling below.
- Blur normalization uses an exact bounded Q24 reciprocal instead of division
  per sample. Hills are filled by contiguous rows. Physical-pixel packing
  reuses bilinear samples and an exact 256-byte quantization lookup.
- The versioned [math API](NATIVE_MATH_API.md) routes vertical sum additions/
  subtractions to bundled ESP-DSP S3 SIMD and accelerates bulk clears/copies.
  The app retains a scalar path with identical output. The firmware wrapper
  handles vector alignment, bounded call sizes and safe scalar tails.
- Integer 32ms physics ticks; at most 128ms of elapsed time admitted after a stall.
  Simulation runs at input checkpoints during rendering using live input;
  the rendered frame uses a frozen game snapshot. Rendering uses a 42ms (~24fps)
  start-to-start cap, without an extra delay after a completed frame.
  Scene preparation in app-owned PSRAM overlaps the previous panel update;
  video backbuffer access and publication still wait for video backpressure.
  Actual EPD frame rate depends on hardware scan and grayscale transitions.
  Unchanged idle/paused scenes are not resubmitted.
- Poll(1) yields at raster checkpoints when 8ms has elapsed. Input edges are
  retained through rendering. Main-loop Poll(4) yields while waiting for scans.
  A 3s failed submission deadline exits with a diagnostic. Video stops and the
  capability lease and PSRAM are released before returning from the app.

## How performance was achieved

Earlier optimization passes retained the original 480x270 raster. In 1.0.8,
the owner approved halving scenery resolution in each dimension: scenery
blending and character-centered fog now run at **240x135**. A bilinear expansion
to 480x270 precedes the sharp character/local mist, rounded fade, and UI. The
existing packer performs the second bilinear expansion to 960x540 and dithers
at physical pixel pitch. World coordinates, physics and cached source art keep
their original scale. Thin scenery details are softer; gameplay and UI scale
are unchanged.

### Render once, reuse what is independent of the player

Each depth plane has a near-blurred and a more widely blurred image. The
renderer mixes those images using squared distance from the character, then
fades the result with distance. Squared distance avoids a square root per pixel.
It gives a smooth artistic focus mask, not a physical lens simulation. The
character is drawn after scenery so its silhouette stays readable.

In 1.0.7, geometry and depth blur live in world-space strips, independent of the
camera. Each of four planes has four 256x270 strips, storing two 8-bit images:
the near/depth filter and the wider filter. Integer camera offsets select source
columns directly; no full-screen cache shifts or copies are needed. Only the
character-relative blend, fog, character/local mist, vignette and output packing
run on every frame. The fourth plane separates 1.23x framing branches from 1x
terrain, which previously forced the combined foreground to be rebuilt.

Each strip is generated in a 480-wide scratch viewport with 112 samples of
horizontal halo on either side. The largest cascaded blur footprint is 12;
only the center 256 columns are retained. Vertical boundaries keep the existing
edge extension. World-space object enumeration includes offscreen contributors,
so culling a parent tree/building cannot change pixels inside a neighboring
strip. The four planes are mixed independently using the existing max-darkness
composition; framing branches have their own cached blur instead of being
blurred together with the terrain's union.

Startup shows PREPARING FOREST and warms the initial view plus one lookahead
strip per plane (12 strips). Physics is frozen; input polling, yielding and exit
remain active. Warmup has an explicit 256-work-slice bound. During play, the
scheduler keeps up to three visible strips plus one directional lookahead per
plane. A slice draws geometry or filters/copies at most 64 rows. Each host-loop
iteration performs at most four slices and stops after observing 8ms elapsed;
a single slice can exceed 8ms but still has cooperative raster checkpoints.
Reversing direction cancels obsolete speculative work. Fully completed strips
are published atomically in the app task. Teleports or outrunning lookahead can
still require synchronous missing-strip completion, with input/yield checkpoints.

The pause panel reports CACHE milliseconds per submitted frame separately from
RENDER and PACK. Cache-miss work needed by rendering is included in RENDER;
speculative work between frames appears in CACHE. The counter prevents a lower
render time from hiding cache-building CPU cost elsewhere in the loop.

### Make the remaining loops cheaper without changing rounding

The separable box blur uses sliding sums: add the incoming sample and subtract
the outgoing one. After initializing a row or column, work per output sample
does not grow with radius. Edges repeat the nearest valid sample rather than
introducing a white border.

Normalization replaces division per sample with an upward-rounded Q24 reciprocal:

```c
n = 2 * radius + 1;
reciprocal = (0x1000000u + n - 1u) / n;
average = ((uint32_t)sum * reciprocal) >> 24;
```

For this renderer's bounds (`1 <= n <= 19`, `0 <= sum <= 255*n`), this is exactly
`floor(sum/n)` and the product fits 32 bits. The division is done once per filter
region. This is a bounded arithmetic identity, not permission to substitute the
same formula for arbitrary ranges. Tests exhaust the supported sums and divisors.

Memory traversal matters alongside arithmetic. Hills formerly used many narrow
vertical fills; they now calculate the same top boundary and fill contiguous
rows. The vertical blur keeps 480 running sums in a 960-byte aligned stack array
and reads PSRAM rows sequentially, avoiding repeated column-strided cache misses.

Packing also reuses work. Two logical samples generate four physical columns
in each of two rows, sharing bilinear intermediate values. A 256-byte table
replaces repeated gray-threshold arithmetic. The original integer rounding order
and fixed 8×8 dispersed thresholds are preserved. Exact gray fills remain solid;
there is no frame-dependent noise that could shimmer or trigger extra EPD work.

### Trade a fixed amount of PSRAM for reusable results

With 129,600 logical pixels, the current buffer layout is:

| Storage | Bytes | Purpose |
| --- | ---: | --- |
| Raw plane | 129,600 | Generate one world strip with halo |
| Near/wide scratch | 259,200 | Prepare one strip's two filter variants |
| Composited scene | 129,600 | Prepare a frame independently of the panel |
| 16-bit filter scratch | 259,200 | Horizontal results consumed by vertical SIMD |
| 16 pairs of 256x270 cached strips | 2,211,840 | Four slots per parallax plane |
| Rounded-corner alpha table | 8,100 | One mirrored 90x90 quadrant |
| Low-resolution scene | 32,400 | 240x135 per-frame scenery/fog composition |
| **Total working PSRAM** | **3,029,940** | Allocated once, plus 15 alignment bytes |

This excludes the video subsystem, app code/stack, and small cache metadata and
per-row bounds. The previous viewport cache used 1,296,000 bytes. The strip-cache PSRAM replaces repeated geometry/filter computation without
quantizing cached shades. Version 1.0.8 adds 32,400 bytes for the reduced-resolution
per-frame scene; its source caches retain their existing detail. Allocation failure logs an error and exits
cleanly. Filter scratch doubles as hill-boundary scratch between filter jobs.

### Use the bundled DSP implementation through a safe app API

Version 1.0.2 uses [the native math API](NATIVE_MATH_API.md) for independent
vertical sum additions/subtractions and bulk clears/copies. The ESP32-S3 backend
processes eight signed 16-bit lanes at a time. Sums reach at most 4,845, or 5,100
between adding and subtracting a row, so widening samples to 16 bits enables
vector processing without saturation, changed rounding or lost precision.

Inspecting the actual bundled assembly revealed two constraints that an API
name alone does not describe: its vector entry does not check output alignment,
and its fused instructions preload one extra input vector. The wrapper checks
all pointers, peels safe prefixes, and leaves at least eight valid input elements
for scalar cleanup so that preload stays within the caller's array. Incompatible
alignments use C. Bulk assembly copies/fills accept only aligned whole vectors;
the world-strip cache no longer needs overlapping full-image shifts.

Apps see bounded, versioned operations rather than private assembly symbols.
Calls enforce the owning app task, lengths and overlap rules. The renderer also
has an equivalent scalar path, but the new getter import still requires firmware
1.3.27: a scalar fallback cannot make an unresolved ELF import load on older
firmware. Host tests model the assembly's constraints; only device measurements
can establish whether its extra memory traffic and call overhead pay off in
this workload.

### Keep simulation responsive while rendering and scanning

Rendering, simulation and panel scanning have separate schedules. Physics uses
32ms steps with a 128ms elapsed-time clamp and an eight-step loop bound. Rendering has a 42ms start-to-start
cap: adding a fresh delay after every render would unnecessarily add render
time to the frame interval.

Raster checkpoints poll input and yield when at least 8ms has elapsed. Simulation
advances there too, so a long render does not suspend gameplay. Input edges are
retained between checkpoints. A frozen game snapshot supplies camera, focus and
character positions for the entire frame, preventing a mixture of different
simulation instants. The 8ms threshold is a checkpoint policy, not a hard
real-time latency guarantee.

Scene preparation happens in app-owned PSRAM while the previous panel update
finishes. Packing into the video backbuffer and submission wait for readiness.
Only one prepared scene is retained; unchanged idle/paused scenes are not
submitted repeatedly. This overlaps useful CPU work with panel activity without
creating an unbounded queue of stale frames.

## Lessons learned

1. **Separate image correctness from display correctness.** The first shared
   video repair fixed a demonstrated transition-state defect: accepting another
   frame while pulses remained, and tracking only a completed shade, could miss
   the erase needed after an interrupted transition. It now tracks intermediate
   commanded levels. Firmware 1.3.29 allows retargeting at scan boundaries. However,
   the owner subsequently reported reduced but persistent retained geometry in
   both games. The software-model tests do not establish that physical ghosting
   is fixed. See [NativeVideoGray.h](../src/native/NativeVideoGray.h) and the
   current limitation below.
2. **Cache only data whose dependencies are understood.** Integer translation is
   reusable; character-centered focus, viewport edge extension and changing
   object selection need separate handling. Cache equivalence should cover
   reverse motion, stationary cameras, culling boundaries and teleports.
3. **Optimize access patterns before assuming arithmetic is the whole cost.**
   Contiguous PSRAM traversal, fewer tiny fills and reuse of filtered planes
   address work that vector instructions alone cannot remove.
4. **Prove arithmetic substitutions over their actual domain.** Exact reciprocal
   normalization and lookup quantization preserve the image because their ranges
   and rounding are checked. Larger blur radii require revisiting the proof,
   intermediate widths and tests.
5. **Treat acceleration as an implementation detail of a stable contract.**
   Alignment, over-read behavior and SDK-version differences belong behind the
   reusable wrapper. Linking an optimized routine proves availability, not speed.
6. **Measure CPU time, responsiveness and panel delivery separately.** A faster
   renderer can improve input handling and leave more scheduling time even when
   physical grayscale transitions still limit visible frame rate.

## Known limitation: retained geometry on the physical display

On September 27, 2026, the owner reported that the latest tested firmware
improved but did not eliminate geometry ghosting in both Risc Strike and Hollow
Trail. The exact installed build was not supplied with that report. Treat this
as an unresolved hardware-visible defect, not an accepted rendering result.

A supplied photo of the running game shows dark foreground trees still present,
with pale, displaced outlines of previous branches and scenery spread across the
lighter background. The physical image looks substantially more washed out than
six captured host-renderer frames during the first 3.52 seconds of moving right;
those frames have no retained silhouettes and their cached filters match fresh
filters exactly. This comparison supports investigating the shared gray-drive
path, but a photograph of one state cannot prove which electrical pulse or
transition caused the residue. The photo was supplied before there was a
confirmed on-device test of the firmware 1.3.28 correction.

Source inspection shows that Risc Strike clears and rebuilds its full submitted
buffer, while Hollow Trail clears its composite scene and repacks every output
pixel. Both request full-height updates. This rules out an intentionally partial
app redraw as the explanation; it does not rule out a lower-level delivery fault.

Firmware 1.3.28 tried erase-before-redraw: three white passes before drawing
changed nonwhite pixels, with frame admission blocked until settling. The owner
rejected this on hardware: a clean final image, but roughly one-quarter the
previous frame rate and bright white flashes during movement in both games.
The supplied video also shows conspicuous white scenery outlines during motion.

Firmware 1.3.29 removes that per-frame erase. Each pixel receives only a pulse
toward its target, while unchanged pixels receive no pulse. Every intermediate
commanded level is retained, so a new frame can retarget at the next scan
boundary instead of waiting for the old target to finish. The pending flip still
protects the double buffer; startup/idle waits still include outstanding pulses.
Only unknown startup contents receive the explicit three-pass white reset.

The gray DMA completion callback now lowers CKV before publishing completion.
Previously CKV stayed high while the CPU prepared the next row, so PSRAM work,
scene-dependent transition calculations and task scheduling could extend row
selection. Ending it in the callback removes next-row preparation from that
interval. Transfer submission and interrupt latency remain; this is not a
hardware-timed or calibrated grayscale waveform. Monochrome timing is unchanged.

Direct steps track commands, not measured optical shades. Black and white drive
need not be physically reversible, and this change does not prove that retained
silhouettes are eliminated. Hardware validation must establish gray fidelity,
residual images and actual frame cadence with the shorter row-selection timing.
The tests cover all 16 direct transitions, 16,384 interrupted target sequences,
startup resets, stable neighbors and admission during outstanding pulses. They
intentionally do not use an invented optical model as proof of panel behavior.

The compared T5S3-GameBoy source produces gray-looking graphics using fixed
black/white dithering (`src/gbemu.c`, `shade_to_white`), driving binary endpoints.
That is a useful responsiveness reference but a different optical workload.
The owner subsequently approved fixed monochrome dithering as the default for
Hollow Trail and Risc Strike, with native grayscale available manually. Lighting,
fog and blur remain scene effects before final output quantization.

See [NativeVideoGray.h](../src/native/NativeVideoGray.h) for the production
sequence and [the transition tests](../test/native_apps/native_video_gray_test.cpp).
This is a corrective implementation candidate; the reported physical ghosting
is not declared resolved until checked on the device.

## Evidence and remaining measurements

| Evidence | Result | What it establishes |
| --- | --- | --- |
| Historical 200-frame host comparison, 1.0.0 versus pre-SIMD optimization | About 6.6ms → 2.9ms/frame; about 2.3× faster; identical packed hashes | Combined renderer optimization benefit on that host, not individual technique attribution or device FPS |
| Cached versus fresh rendering and arithmetic checks | Pass | Cache and arithmetic equivalence for tested cases |
| Math API versus scalar rendering, alignment/tail/overlap tests and ASan/UBSan | Pass | Output equivalence and wrapper safety under the host model |
| Render-service test | Pass | Simulation progresses during rendering while the frame snapshot stays coherent |
| Firmware 1.3.27 T5S3 PRO build and link map | Pass; real AES3 routines linked | Device build integration, not measured SIMD speedup |

The original host timing is a development observation, not a portable benchmark
with a published hardware/compiler baseline. Do not extrapolate it to the S3.
The existing scan target is 24 scans/s. The rejected 1.3.28 path could need six
scans (about 250ms before other overhead), blocking new frames throughout.
Firmware 1.3.29 uses at most three direct steps for a fixed target and admits a
new target at each scan boundary. This restores pipeline overlap rather than
promising any measured frame rate: the app's approximately 24fps cap is not a guarantee of 24
fully settled grayscale frames per second. CPU optimizations, frame admission
and physical settling must be measured separately.

The next useful device comparison is scalar versus DSP with the same camera
path and output hashes. Measure scene preparation, packing, time waiting for
video readiness, completed frame cadence and input-to-simulation latency
separately, including warm-cache scrolling, culling changes and full rebuilds.
Report distributions and worst observed stalls as well as averages. Those
measurements remain outstanding; the additional SIMD gain is not yet quantified.

## Build and verification

App identity: `hollow_trail`; packed monochrome update **1.0.8 → 1.0.9**.
The half-resolution scenery update was 1.0.7 → 1.0.8.
The scenery-cache update was 1.0.6 → 1.0.7.
The fused compositor update was 1.0.4 → 1.0.5.
See [native math operations](NATIVE_MATH_API.md) for signed multiplication
and its exact blend identity. The movement pacing update was 1.0.3.
The simulation step changes from 16ms to 32ms, halving movement speed to
78.125 logical pixels/s (156.25 physical pixels/s). Jump and camera timing slow
with movement, preserving the existing jump paths, pit reachability and level
layout. Input polling and render cadence are unchanged. The DSP update was 1.0.2.
Controller provider setup and mappings are unchanged.

```sh
python scripts/build_native_app.py Apps/hollow_trail.c --output dist/apps/hollow_trail.elf --require-manifest
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function -Ilib/NativeApps/include test/native_apps/hollow_trail_test.c -o /tmp/hollow-test
/tmp/hollow-test
```

The test traverses the complete route, verifies the automatic level loop,
checkpoint recovery, lethality of every pit, deterministic rendering, and all
four packed shades, cached-versus-fresh filter equivalence, and exact blur/
quantization arithmetic. The render-service test verifies that gameplay moves
while one coherent frame is being rendered. Also run with ASan/UBSan for raster
bounds checks. These tests
are included in `test/run_native_app_test.sh`. Hardware refresh speed and gray
response remain to be measured on the device; host timings do not establish
on-device performance.

## Fused compositor in 1.0.5

The blend/fade/max-composite pass now walks each row once. Instead of preparing
three signed rows, making two multiply calls, and rereading their results, it
uses `near*256 + (wide-near)*radial` for the same weighted sum. The original
rounding after blending and after fading is preserved. This portable app kernel
removes 2,880 bytes of PSRAM scratch and needs no new firmware API. Existing DSP
add/sub for blur and copy/fill remain in use; the multiply API remains available
to other consumers.

Squared radial distance advances with `distance += delta; delta += 2` along a
row, and the y-square advances similarly between rows. Focused pixels directly
use the near mask; fully fogged pixels directly use the faded wide mask. Only
the intervening ring computes the fractional mix. These are exact substitutions
for the old clamped formula, including integer rounding at each boundary.
Service checkpoints remain every 32 rows.

The compositor regression checks every output pixel against the pre-fusion
formula across all three layers, twelve focus positions including offscreen
and threshold cases, and nonzero existing scene content. ASan/UBSan passes.
A host `-O2` timing of 500 layer composites measured 203.80 ms for the original
scalar formula versus 124.44 ms fused (1.64x). This isolates compositing; it is
neither a full-game FPS measurement nor a benchmark of actual S3 DSP assembly.
Reproduce with `hollow_trail_composite_test.c --benchmark` after compiling it
like the other host tests. Device PSRAM/cache and display timing remain relevant.

The firmware minimum is corrected to 1.3.30, the version that actually contains
the merged math/video changes after the 1.3.29 release-number collision.

The Q14 transform feasibility probe is in
`test/native_apps/model_viewer_fixed_point_probe.c` (compile with `-lm`). It
compares 98,304 transformed coordinates: sampled error reaches 0.3380 pixels
at the viewer's 7x zoom. A thin face also collapses during input quantization,
independent of accumulator rounding. This probe does not execute the SDK kernel
or establish its device speed. The Model Viewer's float/DSP path is retained;
fixed-point transforms are not enabled as a visually equivalent optimization.

## Frame delivery follow-up

The submission interval is now 42 ms (about 24 FPS), matching the existing raw
scan target instead of capping the app at 15 FPS. This changes rendering cadence
only: simulation remains on the 32 ms step and the panel waveform is unchanged.
Backpressure still prevents writing a queued front/back buffer.

Default monochrome packing has a dedicated loop, reuses adjacent bilinear
samples, assembles complete bytes in registers and writes each destination byte
once. Its four logical samples per byte are explicitly unrolled. The gray path
retains the existing quantizer. The full-frame reference check covers physical
phase, bilinear rounding and bottom/right edge replication with ASan/UBSan.
A 1,000-frame host `-O2` packing comparison measured 506.80 ms before and
351.48 ms after (1.44x), with matching output hashes. Device gain is unmeasured.

The pause panel now shows the last completed measurement window: submitted FPS,
scan counter rate, and average render/pack/backbuffer-wait milliseconds per
submitted gameplay frame. FPS measures accepted submissions, not optically
settled images. Scans are separate because several drive passes may serve a
single image. Timings include cooperative input servicing. Windows update after
at least one second; a window spanning idle time can show a lower average.
Manual display-mode changes reset counters. No serial connection is needed.

The September 27 device clip confirms the owner's report that dithering improved
visual defects, but cannot identify CPU versus display waiting. The fused
compositor and delivery changes remain in the same unreleased 1.0.5 update.

## 1.0.6: dark perimeter and hidden-pixel work reduction

The screen has a fixed black perimeter, 36 physical pixels wide, fading linearly
into the forest by 76 pixels from each edge (the requested approximate 35/75
pixels rounded to the 2x logical grid). Corners use the nearest edge. The fade is
applied after the character; instructions and pause text stay inside the border.
World coordinates, camera, jump paths, and movement speed are unchanged.

This is more than an overlay. Each depth compositor visits only 444x234 samples,
versus 480x270: **19.8% fewer composited pixels**. Foreground blur computes that
same visible rectangle plus the required four-sample source halo, so trimming
work cannot introduce a convolution seam. Background caches retain their full
extents to preserve correct reuse during camera shifts and direction reversals.
Packing bulk-fills the fully hidden, byte-aligned strips and processes only
448x238 logical samples individually (**17.7% fewer**). These bulk fills write
framebuffer memory; they do not trigger a panel clear or a white flash.

The vignette only shades its transition strips; the unchanged central rectangle
is skipped. There are no new per-frame allocations. Full geometry generation,
background cache maintenance, and panel scans still have costs: pixel-work
percentages are not an FPS promise. The owner's pre-change readings were 2.7 FPS,
11.8 scans/s, render 204ms, pack 55ms, wait 0ms. New hardware timings are pending.

Lesson: an opaque border saves compute only when the renderer and packer avoid
its hidden pixels. Filters also require a retained source halo; cropping their
inputs at the visible edge changes the image. Tests compare cropped convolution
against full convolution, both packed formats against the generic path, and
cached scrolling against fresh renders across reversals and camera teleports.

## 1.0.7: cached scenery and rounded vision fade

The square-corner perimeter from 1.0.6 is replaced by a broad rounded fade:
90-logical-pixel corner radius (180 physical pixels), retaining the approximate
36-pixel solid border and 76-pixel fade extent along straight edges. A small
mirrored alpha table is computed once using integer square root. No square roots
run per frame. Per-row opaque and fully clear bounds skip hidden compositing,
shading and packing work around the curves. Packing retains the bilinear neighbor
sample before filling black bytes, preventing a one-pixel transition seam.
Instructions were moved inside the clear part of the rounded viewport.

Validation includes an independent full-viewport render/filter reference for
all four planes, ordinary scrolling without geometry/filter regeneration in
warmed strips, lookahead at 12 logical pixels/frame, strip boundaries, reversals,
teleports, cancellation, rounded-mask symmetry, and both output formats compared
with generic packing. Scalar versus DSP-wrapper rendering remains equivalent.
See `test/native_apps/hollow_trail_cache_test.c`.

A local 3,000-frame moving-camera benchmark measured **1.4519 ms/frame before →
0.9850 ms/frame after** (32.2% less CPU time), including packing and up to four
lookahead slices per frame. This combines strip caching and the rounded fade;
it is not an isolated cache speedup. Host scalar RAM/timing is not ESP32 PSRAM,
DSP assembly, or an 8ms scheduler-budget simulation.

The owner's measured **1.0.6** baseline is **6.6 FPS, 13.2 scans/s, RENDER 94ms,
PACK 52ms, WAIT 0ms**, versus the earlier 2.7 FPS / 204ms render / 55ms pack.
This device improvement exceeded the prior host border benchmark; no device
speedup is inferred from host ratios. Measurements for 1.0.7 are pending.

Lessons: cache data in the coordinate system where it is static; separate planes
with different parallax speeds; retain convolution halos; and include lookahead
CPU time in performance reporting. A cache must remain bounded and publish only
complete images. Correctness tests must compare against an uncached renderer,
not simply against a second execution of the same cache algorithm.

## 1.0.8: half-resolution scenery, sharp overlays

The 240x135 scene has one quarter as many samples as the original 480x270
surface. Every layer's near/wide mixing, radial fog and max-darkness composition
now use this grid. Each sample reads its original world location (2x, 2y) from
the existing strip cache. Odd integer parallax offsets remain intact: crossing
a strip edge with an odd offset must resume at the correct next source column.
The existing solid-border rectangle still bounds the costly shader loops.

An explicit 2x bilinear expansion writes the 480x270 overlay surface. Character,
eye details, limbs, local separation mist, rounded vignette and UI are then drawn
at their previous resolution. The existing final 2x bilinear packer supplies
960x540 output and physical-pixel dithering in either display format. No world
coordinates, physics constants, collision surfaces, jump paths or camera timing
are rescaled. The rounded viewport retains its physical size.

Retaining the precomputed artwork/blur caches avoids rebuilding assets or
introducing an additional cached-texture quantization. The per-frame compositor
is reduced; the infrequent cache-building raster remains full resolution.
Packing and display scans still cover physical output and cannot be expected to
become four times faster. The visible tradeoff is softer/coarser fine scenery,
particularly branches and grass; the character and text avoid that reduction.

Tests compare cached low-resolution composition against independently rendered
full-resolution geometry and filters sampled on the new grid, followed by an
independent per-output-pixel interpolation reference. They cover odd offsets,
strip boundaries, reversals and teleports. A separate randomized interpolation
check covers all pixels, rounding and last-row/column clamping. Gameplay,
input-service, scalar/DSP-wrapper and both packing-format checks still pass.
Two before/after scene previews were inspected using the actual renderer.

A 3,000-frame host comparison including packing and lookahead measured
**1.0320 → 0.5987 ms/frame** (42.0% less CPU time). This is a host scalar benchmark,
not an ESP32 speedup prediction. The owner's latest device baseline is
**RENDER 141ms, PACK 44ms, CACHE 8ms, WAIT 0ms, SCANS 10/s**, with approximately
5.5 FPS and improved consistency. Device measurements for 1.0.8 are pending.

Lesson: lowering the per-frame lighting grid targets the remaining dominant
loop without sacrificing input precision or UI readability. Upscaling must be
explicitly budgeted, and source-cache, world, overlay and physical pixel units
must remain distinct. Final dithering stays at display resolution.

## 1.0.9: four-pixel integer packing

The default monochrome packer processes four logical pixels in independent byte
lanes of 32-bit registers. Two aligned source-word reads and two neighboring
samples replace the per-pixel source-loading loop. The existing bilinear
intermediates are computed with an exact byte-lane average:

```c
(a & b) + (((a ^ b) & 0xfefefefeu) >> 1)
```

Each lane retains floor((a+b)/2), with no carry into neighboring bytes. Horizontal,
vertical and diagonal interpolation retain both original rounding steps.

The fixed Bayer matrix's four quadrants have thresholds within distinct 64-value
ranges. The packer converts `value > rank*4+2` into `value >= rank*4+3`, then
compares four lanes together. Setting each source lane's high bit before
subtracting a threshold below 128 prevents cross-byte borrowing. The original
source high bit determines the result for values in the other half-range.
Gathering the four comparison flags puts them directly into alternating output
bits; a second set supplies the intervening bits. Both physical rows are written
as complete bytes, preserving the existing screen-fixed dither pattern.

This is ordinary bounded 32-bit integer code, with no new firmware API, DSP call,
per-frame allocation, large lookup table, or working-PSRAM increase. The threshold
table is 16 bytes. Helpers are explicitly inlined for the app's `-Os` build. The
Xtensa assembly was checked: the source reads use aligned word loads and the
pixel loop has no memcpy calls. The source alignment follows the existing aligned
`ht_bind` storage, 480-byte row stride, and four-pixel block starts.

Tests exhaust 65,536 input pairs for lane averaging with differing neighboring
lanes, all byte values across lower/upper threshold halves, and full-frame
packing against independent per-physical-pixel interpolation/dithering. The
frames include rounded-border scenery and unframed random data, exercising
right/bottom clamping. ASan/UBSan, gameplay/border packing checks, and the target
app build pass. The native grayscale path retains its existing quantization.

For 1,000 host packing runs of a rendered scrolling scene: **309.89 → 167.84 ms**,
45.8% less CPU time, identical output hash `fca693da`. A seeded unframed comparison
also retained hash `e1f582ab` (357.77 → 196.10 ms). These are packing-only host
benchmarks, not measured ESP32 speedups or total-frame gains.

The owner's 1.0.8 baseline is **8.4 FPS, 8.4 scans/s, RENDER 56ms, PACK 43ms,
WAIT 10ms, CACHE 6ms**. Device measurements for 1.0.9 are pending. The new packing
code does not move work onto the scan core or change the panel waveform.

Lesson: structured thresholds allow exact parallel arithmetic within ordinary
integer registers. Preserve interpolation rounding and screen phase, prove lane
independence, and inspect the actual target compiler output; a faster host loop
alone does not establish faster device execution.


### DSP16 compositor A/B toggle (1.0.10)

Hollow Trail now always starts and stays in monochrome dithered mode. The former
pause-menu display shortcut switches only cached scene compositing between the
existing scalar path (default OFF) and a batched signed 16-bit multiply path.
It does not stop/restart video, invalidate scenery caches or change packing.
Existing DSP cache filtering and memory operations remain enabled in both modes.
This makes the comparison specific to per-frame blur interpolation and fog.

Each cache span supplies at most 128 samples to two multiply calls through the
existing math API. Four 16-byte-aligned 128-element buffers consume 1,024 bytes
of app BSS. The bridge executes eight-lane S3 multiplication with safe scalar
tails. Signed 16-bit products cannot represent 255*256: weights are split into
low seven bits and a 0/128/256 correction accumulated in 32 bits. Products stay
within +/-32385, with exactly the original rounding and output pixels. Failed
multiply calls use scalar multiplication; absent S3 support shows UNAVAILABLE.
No firmware/API change is required. Buffer preparation and two calls per span
may outweigh SIMD savings; ON is an experiment, not a claimed speedup.

For device comparison: pause with Start/Down, record FPS/RENDER/PACK/CACHE,
press A/Confirm to change DSP16, resume, and move over the same section for
several seconds before pausing again. Changing mode clears the measurement
window; resuming excludes pause duration and paused scans from the next window.
Use repeated OFF/ON runs. Packing and image appearance should stay the same.

Host checks compare DSP-wrapper output against the independent scalar formula,
including signed extremes, all 257 radial weights, short/vector/tail spans,
failed-call fallback, and full cached scenes at tile boundaries and reversals.
These mocks verify arithmetic and integration, not ESP-DSP execution speed.

Host compositor candidate benchmark (12 camera positions, seven trials, 8,400
frames per candidate; cache generation/upscaling/packing excluded):

| Candidate | Host `-O2`, ms/frame | Host `-Os`, ms/frame |
| --- | ---: | ---: |
| Current scalar | 0.1755 | 0.2681 |
| Four-pixel unroll | 0.1605 | 0.3019 |
| Four-pixel unroll plus packed max | 0.1938 | 0.3100 |
| Two-pixel packed far fog/max | 0.2173 | 0.3028 |

All candidates matched the reference bytes. Unrolling helped at `-O2` but lost
at `-Os`, the app's actual optimization setting (on a different CPU). None of
these candidates was promoted into production. Host results cannot substitute
for S3 DSP measurements; the device toggle provides that comparison.

Owner-reported 1.0.9 results: 8.7 FPS, 8.7 scans/s, RENDER 54ms, PACK 23ms,
WAIT 31ms, CACHE 2ms. Compared with the previous 43ms packing measurement, packing
fell about 47%, but prepared-frame wait increased from 10ms. WAIT is elapsed
wall time from completed rendering until packing can start, including driver
backpressure and app-loop work; it is not a deliberate delay. Faster CPU stages
can expose more of that wait without raising total frame rate proportionally.
These measurements are not evidence of the panel's maximum possible scan rate.

### Separate scan core and visible driver timings (firmware 1.3.32, app 1.0.11)

Owner device comparison: DSP16 ON reached 7.4 FPS; scalar reached 8.8 FPS.
The scalar compositor remains the default. The next bottleneck investigation
moves the existing scan task to the opposite core from the video-start caller
(on this dual-core board, scanner 0 / app 1). Previously both tasks were on
core 1. Single-core builds retain their only core. No scan pulse counts,
waveform, panel bus frequency, buffer admission or dither arithmetic changes.
The shared video service change applies to all consumers, not just this app.

The pause panel adds two rows, obtained through a size-checked, optional
`T5VideoApi.scan_stats` callback. The firmware publishes a coherent snapshot
under the existing short video lock, after accumulating roughly one second
of complete scans. Displayed durations are rounded mean milliseconds per scan:

| Field | Meaning |
| --- | --- |
| SCAN | Whole scan-loop work before target-rate pacing, including row submission and logging |
| PREP | Time preparing all rows: pixel-state conversion, drive-byte packing and any idle cleanup |
| DMA | Residual time waiting for the previous DMA transfer, including the final transfer |
| PACE | Time in the existing 24 FPS pacing function after scan work |
| ROWS | Mean active rows converted per scan; physical scanning still covers all 540 rows |
| CORE | Scanner core / video-start caller core; expected `0/1` for Hollow Trail |

PREP and DMA are portions of SCAN, not extra time to add to it. DMA overlaps
row preparation; DMA here is only the wait that remains afterward. Differences
between SCAN and those two portions include row-transfer submission, GPIO,
locks, logging and instrumentation. All timings are wall time and include
preemption. Per-row clock reads add a small, currently unmeasured overhead.
PACE near zero means the scanner is not deliberately waiting for its FPS cap.
The app FPS window and scan profile window are independent; compare sustained
movement, then pause to capture the latest completed windows. Values remain
visible on the static pause screen. Before a window is ready, the panel says
SCAN TIMING NOT AVAILABLE.

Firmware 1.3.31 -> 1.3.32 and Hollow Trail 1.0.10 -> 1.0.11; the app now requires
1.3.32 so an update cannot silently omit the requested core split/timings.
The callback is appended to the version-1 API, preserving older app layouts.
Host accumulator checks cover means, fresh windows and a >32-bit clock.
Hardware throughput and the effects of shared PSRAM/other core-0 tasks still
require the owner's device measurements.

### Scan lookup, bounded dirty rows and batched upscale (1.3.33 / 1.0.12)

The monochrome scanner now builds a 1,024-entry, 2KB lookup in internal RAM
before scanning. Its key is the old two-pixel state byte plus two target bits;
its result contains the next state, four drive bits, target-change flag and
unfinished-drive flag. Four lookups process one source byte (eight pixels),
replacing the nested per-pixel branches and counter arithmetic. The table
preserves all original state transitions and drive commands, including target
reversals during the three-pass drive sequence. Grayscale uses its existing
path. The finite idle reinforcement policy and 24 FPS scan target are unchanged.
This speeds shared monochrome video consumers, not just Hollow Trail.

Hollow Trail still packs the complete backbuffer, but after the first successful
full-screen submission it marks only physical rows 36 through 501 dirty (466
rows instead of 540). Bounds are derived from the rounded mask and include the
preceding interpolation row. Loading and failed initial submissions cannot
skip initialization. The driver's existing active-row state preserves unfinished
border pulses when later submissions mark only the middle rows. This avoids
74 rows of repeated conversion per new target; it does not skip physical panel
scan lines or change the rounded border.

The 240x135 to 480x270 upscaler now loads four source bytes at a time, uses the
same exact byte-lane averaging as the packer, and interleaves samples into four
aligned word stores for eight output pixels in each pair of rows. Both rounding
stages and right/bottom edge clamping are preserved. No new app scratch buffer
is required. Target compiler inspection confirms word loads/stores.

Verification: all 1,024 mono state/target transitions and 40 full scan sequences
match the previous arithmetic's state, drive bytes and flags; sanitizer checks
pass. Existing independent per-output-pixel upscaling, full scene/cache, packing,
input-service and route tests pass. All excluded rows are checked solid black
across rendered route scenes. Firmware and app builds pass; source/sidecar app
version is 1.0.12 and minimum firmware is 1.3.33 (firmware 1.3.32 -> 1.3.33).

Production lookup host benchmark at `-Os` (400 scans per scenario/candidate,
five trials): unchanged targets 1.786 -> 0.461ms, retargeting every scan
4.333 -> 0.444ms, about 74% and 90% less conversion time. This excludes DMA,
PSRAM timing and display output. The optional benchmark is reproducible with
`native_video_mono_test --bench` after compiling the test source.
The upscaler alone is slightly slower on this host: 0.05286 -> 0.05600ms
(medians across seven alternating trials, 3,000 frames per trial, `-Os`).
That is about 6% more host time, not a demonstrated speedup. Word-access benefits
on the ESP32/PSRAM path need device measurement. Matching frame hash: 51f6b1f5.
The new on-screen PREP/SCAN and RENDER counters distinguish the improvements;
no device FPS or optical result is claimed from host tests.

### Overlap packing with display wait (Hollow Trail 1.0.13)

The former pipeline rendered a scene, waited for `can_submit`, then packed it.
This serialized packing behind the previous queued frame even though packing
only needs app-owned scene data. The new path checks readiness after rendering:
if the display is busy, it packs immediately into an optional 64,800-byte PSRAM
staging buffer. Once the display releases its backbuffer, the app copies the
finished bits and submits. If the display is already ready, it packs directly
into the backbuffer, avoiding the extra copy. Allocation failure preserves the
old direct path and logs that staging is unavailable. No firmware change/API or
scan-rate increase is required; minimum firmware remains 1.3.33.

There is still only one prepared app frame and one queued display frame. Staging
remains immutable across waits and rejected submissions. Display-owned memory
is acquired/written only after readiness. Copying uses fixed 1,920-byte chunks
with existing input/yield checkpoints and exit handling. A ready prepared frame
skips a redundant fixed input delay if input was serviced within 8ms; busy waits
poll/yield with 1ms rather than 4ms. Cache prefetch checks display readiness before
each slice and yields priority to a prepared frame as soon as possible.

PACK measures packing in either destination. WAIT measures time remaining after
the pixels are prepared until output transfer starts. The added COPY field
(next to CACHE) reports staged-copy time separately, including its checkpoints;
it is zero on the direct path. Compare FPS and RENDER+PACK+WAIT+COPY together:
a smaller WAIT by itself is not proof of higher throughput. Extra memory traffic
can offset the overlap benefit, and the 24 FPS driver target remains intact.

An app-main test uses a delayed mock display, checks packing occurs while busy
without any writes to its queued buffer, verifies submitted staged bytes, forces
a rejected submission, and exercises staging-allocation failure and cleanup.
ASan/UBSan and input-service tests pass. Physical display timings remain for
device measurement; no FPS gain is asserted from this host test.
The pause panel also identifies the app version to disambiguate device reports.
Version: Hollow Trail 1.0.12 -> 1.0.13.

### Reduce scan preparation traffic (firmware 1.3.34)

Owner timing was SCAN 115ms / PREP 108ms / DMA 1ms / PACE 0, with scanner/app
cores 1/0. That places the measured cost in preparation, not residual DMA or
target-rate sleep. Installed app/firmware versions were not supplied with that
reading; ROWS 540 does not establish that the 466-row update was active.

The shared mono converter now uses one aligned 32-bit state read and one write
per eight pixels instead of four byte reads and four byte writes. The firmware's
heap allocation and 480-byte row stride guarantee alignment; a compile-time
stride assertion documents it. A new 256-entry settled-word table adds 1KB of
internal RAM (3KB total lookup storage). If a state word exactly matches the
canonical settled state for the eight target pixels, conversion emits zero
drive and skips all transition lookups and state writeback. In-progress,
changed, startup and noncanonical states still run the original lookup rules.
Idle reinforcement remains a separate later pass and therefore still works.

The bounded converter is explicitly non-inlined into an IRAM section, avoiding
flash instruction-fetch contention while app and scanner access external RAM.
The target compiler probe emits a 229-byte converter with word state accesses
and no memcpy calls in the converter. This probe used the real Xtensa GCC and
a minimal attribute-header stub; it does not substitute for a full firmware
link. No app ABI, waveform command, pulse count, scan cap or frame admission
change. No app version bump is required: existing monochrome video consumers
benefit when firmware is upgraded from 1.3.33 to 1.3.34.

Sanitizer regression checks cover every two-pixel state/target combination,
100 mixed-state rows, all 256 settled words with poisoned output/canaries, and
40 full scans including retargeting every scan and later settling. State, drive
bytes and changed/pending flags match the original arithmetic. Optional
`native_video_mono_test --bench` now compares against the shipped byte-access
lookup, not only the much older branch-based converter.

Host `-Os` comparison against that shipped lookup, five alternating trials of
80 scans each: seeded unchanged targets 0.265 -> 0.069ms (74% less); seeded
random targets changing every scan 0.266 -> 0.295ms (11% more). With eight actual
Hollow Trail views at camera 1100..1170, stepping 10 logical pixels and using the
production renderer/packer: unchanged target 0.260 -> 0.071ms (72.6% less),
scrolling targets 0.278 -> 0.213ms (23.5% less). These exclude DMA and do not
simulate ESP32 PSRAM or the benefit of IRAM. The shortcut helps coherent game
images; it is not a universal win for random noise. Device PREP/SCAN/FPS and
optical measurements remain necessary to establish the physical result.

## Skyscrapers and Oil Fields (1.0.14)

The second route has its own rooftop collision map, with climbs, drops and nine
lethal gaps. Movement and jump physics are shared with the forest. Checkpoints
on roofs 4, 7 and 10 survive falls within the city; changing level resets the
checkpoint. The oil-field route has its own ten ledges and checkpoints. A lap is a complete
ten-chapter circuit.

Two deterministic skyline layers replace hills and trees: distant pale towers,
closer darker buildings, stepped crowns, antennas and subdued windows. Playable
roofs remain dark with readable ledges. Close crane beams/cables use the existing
foreground plane. The oil-field layers contain mesas, derricks, static pumpjacks and cactus;
nearby gantries frame the top edge. All settings retain radial fog, depth blur, fixed dithering and
the rounded vignette.

City/desert geometry and both blur footprints enter the same bounded strip cache as the
forest. No additional framebuffer/cache allocation or per-frame geometry pass is
introduced. Level changes discard speculative cache work and invalidate strips;
physics pauses during the bounded warmup while input polling/yield/cancel remain
active. The last submitted picture stays on screen, without a white clear. Each
render uses a fixed level snapshot even if input service reaches a doorway while
that frame is being drawn. Prepared old-level output is discarded at the next
loop boundary. Startup/transition cache costs are separate from steady movement;
no new device FPS measurement is claimed.

## Ten-chapter narration

`Apps/hollow_trail_story.inc` holds the authored chapter titles and 30 passages.
Each passage is two short lines. The first appears at chapter entry; later ones
unlock at world x=1100 and x=2600. Text is steady, without a typewriter effect or
expiry timer. Stopping or pausing lets the player read at their own pace. Pause
shows the existing diagnostics; resuming restores the current passage.

The chapter remembers its furthest position through deaths and backtracking,
so narration never jumps backward after a fall. Changing chapter resets that
progress. Gameplay/render snapshots include this position, keeping text in sync
with the picture even when input service advances simulation during rendering.
White text is drawn after the vignette on a dark backing in logical rows 232–250,
inside the existing physical dirty band. It sits below the standing character
and landing surfaces; a falling character may pass behind the margin.

The original three routes are followed by seven separate collision profiles and
cached scenery sets. No additional scene buffers or cache slots are allocated.
Geometry is still procedural in this version; SD level packs remain a future
plan, not an implemented dependency. New motifs and blur are prepared only when
strips enter the cache. Text composition adds bounded glyph work each frame.

| Chapter | Setting | Story development |
| --- | --- | --- |
| 1 | Dark Forest | Three flashes lead to the missing sister's trail. |
| 2 | Skyscrapers | A timed beacon and map point west. |
| 3 | Oil Fields | A silent radio directs the search to the railway. |
| 4 | Silent Railyard | Supplies show she expected someone to follow. |
| 5 | Drowned Marsh | Ferry shelters and notes lead upstream. |
| 6 | White Quarry | Small footprints reveal she found other survivors. |
| 7 | Glass Gardens | A child's drawing reveals she helped shelter them. |
| 8 | The Broken Dam | Restored power supports a signal on the ridge. |
| 9 | The High Pass | Her scarf marks the final climb to the beacon. |
| 10 | The Last Light | A radio reply and three flashes promise reunion. |

Validation covers a full ten-chapter circuit without deaths, every lethal pit,
chapter-local respawns, narration thresholds/retention, glyph bounds, dirty-row
coverage, and cache equivalence against independently rendered scenes in every
setting. The Xtensa app build and six host sanitizer suites pass. These checks
and inspected engine previews do not establish physical-panel FPS or legibility.
The cumulative unreleased app version remains 1.0.14 (master/published 1.0.13).

## Half-width vignette (1.0.14 follow-up)

The solid straight-edge border is now 18 physical pixels (previously 36),
with the fade reaching full scenery at 38 pixels (previously 76). The rounded
corner radius is halved as well, from 180 to 90 physical pixels. The earlier
border measurements above describe the original implementation.

The newly exposed area is rendered and packed normally. Dirty rows are derived
from the mask, now rows 18–519 inclusive (502 rows), rather than a hardcoded
crop. Half-resolution lighting uses the actual even sample coordinate when the
logical border is odd. Cache requests include those samples. Prefetch chooses
the next strip by distance until visibility, adjusted for each layer's parallax
speed, rather than always prioritizing the far background. The four-slice test
still keeps up with 12px camera steps; the app's work/time budget is unchanged.

Independent cache comparisons now render overlapping viewports to provide real
blur halos at the newly visible edges. All ten settings, scalar/DSP equivalence,
packing and narration dirty coverage pass the existing six sanitizer suites.
The Xtensa app build passes. More scenery is now visible and scanned; device FPS
with this wider view remains unmeasured. App version remains the cumulative
unreleased 1.0.14, above master/published 1.0.13.

## Chapter puzzles (1.0.14 follow-up)

Each chapter ends with a safe puzzle area on ledge ten. Three mechanisms sit at
world x=2900, 2985 and 3070, before the locked exit. Walk within 22 logical pixels
of a mechanism and press controller B or device/navigation Up. Interactions need
a fresh press and grounded feet: holding the button or jumping past a control
cannot activate it repeatedly. A/Confirm remains jump; Up retains its existing
DSP-toggle alias while paused. The highlighted line below a control identifies
which one is in reach. The last ledge reminds the player to consult discovered evidence, with a
mechanism name and controls above the scene. Operational clues are now found
throughout the chapter, as described below.

Three puzzle rules are reused across ten authored scenarios: ordered signals,
binary switches, and dials that cycle 0–3. Signals display accepted-note count;
an incorrect note restarts the attempt (and can itself be the first correct
note). Switch/dial values are visible on their faces. Nothing is timed, there is
no failure damage, and clues remain available. Solutions latch, remove the exit
bars, light its beacon and reveal a two-line story fragment. Both partial and
solved state survive chapter-local deaths, while a chapter transition resets
state. Checkpoint ten respawns before all controls, preventing a locked-out
return. The exit cannot be bypassed with a jump.

The fragments gradually reveal that the route predates the sister, that she
helped other survivors, and that the settlement kept the beacon alive together.
The last puzzle asks the player to send the three flashes from the opening.
All story text is authored locally; no network, SD assets or audio is required.

### Puzzle solutions (spoilers)

Positions below are left/middle/right; dial values are given left to right.

| Chapter | Mechanism | Solution | Discovery |
| --- | --- | --- | --- |
| Forest | Mill bells | Right, left, middle | An older map marks the same trail. |
| Skyscrapers | Rooftop relay | Outer switches on, middle off | The beacon guides anyone lost. |
| Oil Fields | Fuel valves | 1, 3, 2 | Supplies supported a group. |
| Railyard | Rail signals | Only middle on | Children thank the lamp keeper. |
| Marsh | Ferry bells | Left, right, middle, left | She waited for every traveller. |
| Quarry | Hoist drums | 3, 1, 2 | Three children travelled with her. |
| Gardens | Shutters | Left off, others on | The children tended the gardens. |
| Dam | Water regulators | 2, 1, 3 | Many hands maintained the signal. |
| High Pass | Ridge chimes | Middle, left, right, middle | She kept a light for the player. |
| Last Light | Answering lamp | Left three times | The final gate opens toward reunion. |

Puzzle state is part of the existing frame snapshot. Machinery is a small
sharp overlay; changing a switch never invalidates cached scenery or blur, and
no new frame buffers are allocated. The gate is enforced in simulation as well
as drawn. Input service requests a redraw even when the character is stationary.
Tests traverse all chapters while operating the mechanisms, check independent
explicit solutions, retries, gate enforcement, chapter resets and death
retention. Production-input tests cover held-button suppression and loading.

## Hidden evidence and journal (1.0.14 follow-up)

Thirty authored evidence objects are distributed across the ten settings, three
per chapter on ledges 2, 5 and 8. Small scraps, containers and glints belong to
the scene; there are no distant arrows or flashing collectible markers. A title
and inspect prompt appear only within 22 logical pixels while grounded at the
object's elevation. Early and late objects sit near the beginning of their
ledges; the middle object is toward the far end. They are reachable on the
ordinary route, with no new jumps or mandatory inventory checks.

Examples include scarf stitching, a mill register, a window sketch, a burnt
fuse, fuel labels, railway tickets, ferry tokens, hoist notices, garden records,
water gauges, a polished signal mirror and the last radio card. Each object has
four short lines combining a practical clue with story evidence. Puzzle logic
is divided between these objects instead of automatically printing the complete
solution at the exit. The player may still solve a puzzle through inference or
experimentation without collecting every note.

Controller B / device Up inspects a nearby object and opens its journal entry.
Near puzzle machinery it operates that mechanism instead. Anywhere else it
opens the journal. Left/Right browses all thirty pages; undiscovered entries show
only NOT FOUND and exploration instructions, never their titles or clues.
B/Up, A/Confirm, or Back closes the journal. Back while reading does not also
exit the app; a separate subsequent press exits normally. Start/Down also closes
reading. Physics freezes while the journal is open, and close/navigation input
cannot leak into jumping or puzzle operation. Paused diagnostics remain separate.

A 30-bit discovery mask is part of the game snapshot. Finds survive deaths,
chapter transitions and the replay loop **within the current app session**.
Exiting the app clears them; SD save/resume is not implemented. Journal content
and page are snapshotted before rendering so input checkpoints cannot mix pages
or reveal new discoveries in an older frame. Small object overlays do not
invalidate terrain/blur caches or allocate another framebuffer.

Validation includes inspection distance/elevation/grounding, duplicate finds,
all thirty records, death/chapter retention, masked undiscovered pages, text
bounds, production-input journal opening/navigation/freezing/closing, held-key
suppression and Back behavior. The existing full-route, cache, compositor,
pipeline and dithering checks remain in place. Native app version remains the
cumulative unreleased 1.0.14, above master and published 1.0.13.

The vignette mask allocation is rounded up to 16 bytes so the reduced scene
that follows it remains aligned after the corner radius was halved. This adds
seven padding bytes for the 45px radius and preserves the word-load contract.

## CI build corrections

The strict C++ native-video test includes the C game engine. Resetting puzzle
state with a C compound literal `{0}` triggered `-Wmissing-field-initializers`
under that C++ build even though the native C app compiled. Reset now uses the
same explicit `memset` form in both languages.

The T5S3-Pro linker also rejected the in-class IRAM converter: its C++ inline
COMDAT literal pool was emitted as `.literal.<method>` in flash, after the IRAM
instruction using Xtensa `l32r`. A file-local, non-member IRAM implementation
keeps its literals in `.iram1.0.literal` alongside the converter, with the member
as a thin wrapper. Word access, transition tables and settled-group skips are
unchanged. An isolated Xtensa compile/link reproduces the original dangerous
relocation and links the corrected version with separate IRAM/flash addresses;
it uses a minimal section-attribute header and linker script, not a full board
link. The native C++ transition test still verifies all 1024 transitions and 40
full scans. Firmware/app versions remain the cumulative unreleased 1.3.34/1.0.14.

## Reader journal (app 1.0.15 / firmware 1.3.35)

The 30 objects now open full found documents (roughly 130–180 words each),
with physical details, different witnesses and the traveller's reflections.
The operational directions remain explicit inside each document. Ambiguity
concerns the people, the older trail and their choices, not arbitrary puzzle
wording. Thirty additional reflective passages recap the ten chapters, unlocking
alongside the matching on-trail narration. Recaps never reveal uncollected clues.

B / Up inspects a nearby object, operates nearby machinery, or opens the journal
elsewhere. The journal index contains **collected evidence** followed by **story
so far**. Left/Right selects a record; A/Confirm opens it. In a record,
Left/Right turns pages; A, B, Up, Back or Start returns to the index. From the
index B, Up, Back or Start closes reading. Navigation is edge-triggered; held
Back does not exit the game after closing the journal. There is no page-turn
animation. Both evidence and reached narration survive death, chapter changes
and replay within the current session; quitting still clears progress.

### Typography boundary

The manifest declares the optional `reader.typography` API 1 software capability.
The app acquires/releases it through `T5ProviderCapabilityApi`, alongside its
independent gamepad lease. Firmware validates the declaration and execution
owner; each callback checks its lease generation and owner, and loader cleanup
invalidates outstanding leases. This is a built-in software provider for the
resident reader, not an installed hardware provider or a new direct font import.
The app never sees `CrossPointSettings`, a `GfxRenderer`, SD font objects or a
physical display handle. The public ABI is `sdk/driver/RiscReaderTypographyV1.h`.

The service selects `SETTINGS.getReaderFontId()` after preparing the selected SD
family when applicable, so family **and size** follow the ebook configuration.
Line compression follows the reader setting too. Text is measured with the same
font used to draw it, including the renderer's kerning and ligature handling.
It draws into an app-owned, native-resolution 960x540 monochrome bitmap. Font
pixels never pass through the reduced-resolution scenery compositor or upsampler.
The detached renderer shares font registrations but cannot alter the host's
orientation, framebuffer or display mode. The capability does not refresh the
panel. A missing/failing service displays an explicit typography-unavailable
message and allows returning to the game; it does not silently replace the
selected reading font with the game's tiny bitmap face.

Pages use bounded UTF-8 byte offsets. The app remembers up to 64 page starts
per opened record; reopening starts at page one. Content is immutable during a
reading visit because physics is frozen. Input during display copying queues a
later revision rather than changing the prepared bitmap. There is one additional
64,800-byte PSRAM reading bitmap and a 4 KiB text buffer; nothing is allocated per
frame or per page. Normal gameplay does no typography work.

The service limits text to 16 KiB, title/footer to 128 bytes, target storage to
256 KiB, and uses a 255-byte line buffer. It yields after each 240-byte font
metric preparation chunk and each laid-out line, with a five-second deadline;
bitmap polarity conversion yields every 32 rows. Existing font loading remains
inside the reader subsystem. Invalid requests and failures return false and the
caller must discard the target. The service does not hold an app buffer after
returning. Entering a journal page and the first restored game frame submit all
rows through the existing fast video driver; this is **not** a clear/rewrite
cycle, and unchanged reading frames retain the driver's idle cleanup behavior.

Validation: native Xtensa ELF build; production-app queue test verifies native
bitmap copying, full-screen entry/restoration, lease release, delayed submission
and fallback allocation; input tests cover page history, reading freeze and
unlocked records. Host tests compile the production typography service with fake
font/device dependencies to verify configured font/spacing, polarity, capacity,
progress and timeout. Separate layout tests exercise proportional measurement,
UTF-8 boundaries, paragraph breaks, long words and cancellation. These are not
physical display or SD font performance measurements.

Versions: Hollow Trail 1.0.14 -> 1.0.15; firmware 1.3.34 -> 1.3.35, both above
master and the published release index checked for this change. The app requires
firmware 1.3.35 for the new typography capability.

## Contending accounts and the final decision (1.0.15 revision)

[The implemented story and ending design](HOLLOW_TRAIL_STORY.md) supersedes the
straightforward rescue/reunion synopsis above. All 30 documents, 30 narration
beats, chapter recaps and puzzle unlock messages now sustain the sister's and
keeper's conflicting accounts. Her coercion, manipulation and leadership remain
plausible as the same observations acquire different explanations.

The last tower now requires **Break the circuit** or **Complete the circuit**,
with a separate confirmation. The choice changes the final evidence entry and
adds its result to the journal's witnessed endings. The opposite ending is
hidden until played. Read the result and press A/Confirm on its last page before
walking through the exit to restart. B cancels or returns to the journal without
silently completing the chapter. Both choices and the replay flow have host
input tests; the existing full-route test exercises the new exit gate.

A reader capability failure now shows a labelled compact fallback with complete
text and pagination rather than leaving the final gate impossible to complete.
Normal reading still uses configured typography through the same capability.
The app remains the cumulative unreleased 1.0.15; this revision adds no firmware
changes or extra frame buffers. Journal/ending history remains session-only.


## Input scheduling experiment (1.0.16 / firmware 1.3.36)

Owner baseline on 1.0.14: approximately 12 FPS, RENDER 59ms, PACK 18ms,
SCAN 23ms, PREP 10ms, DMA 8ms and PACE 19ms. Render/pack include input
checkpoint time. PREP/DMA are scan components; display scanning overlaps app
work, so those numbers must not all be added together.

The host's original `poll`, including `poll(..., 0)`, deliberately delays before
updating input. The controller `poll(..., 8)` argument is a report-count bound,
not an 8ms delay. Firmware 1.3.36 adds optional `poll_nowait`, preserving the
existing yielding call for every older app. Hollow Trail samples input at its
existing raster checkpoints when 8ms have elapsed without that added sleep,
and performs a real yielding poll at the first checkpoint after 32ms since its
last yielding poll. Idle and display-wait loops still yield. The next frame's
initial input poll also drops the old unconditional 4ms requested delay.

The optional member is size/pointer guarded; 1.0.16 still runs on 1.3.35 using
the original yielding input service. Install firmware 1.3.36 for the no-wait
experiment. App minimum remains 1.3.35 because the fallback is functional.

The pause screen adds INPUT, average milliseconds spent inside all input
service calls per submitted gameplay frame (including provider work and
scheduler delays). It overlaps RENDER/PACK/WAIT/CACHE/COPY accounting and is
not an additional frame cost to sum. The startup log identifies no-wait versus
legacy mode. Compare movement in the same chapter, controller and DSP setting;
record FPS, RENDER, PACK, INPUT, CACHE, COPY and WAIT. No device speedup is
claimed before measurement. The input scheduling experiment preserves rendering, packing, narrative,
physics, display waveforms and the 24 FPS cap. The hardware-feedback fixes
below additionally correct A/B actions and reader glyph resolution.


### 1.0.15 hardware feedback included in 1.0.16

Swap A/B actions to match the owner's receiver labels: A inspects/accepts,
B jumps. X/Start/Select retain their masks and A cannot exit gameplay.

The detached reader renderer copied font registrations but left its cache
manager null. Compressed fonts therefore had valid metrics but no glyph
bitmap decompressor: layout reported success and the resulting page was white.
Detached targets now borrow glyph bitmap resolution from their live source
renderer, without inheriting its scan-only recording mode or touching the
physical display. The source must outlive the synchronous page target.
Hollow Trail also rejects successful but entirely white typography pages and
shows the readable compact fallback, including on firmware 1.3.35.


The oil-field cactus arms now use shared elbow coordinates and overlap their
horizontal branches. Previously independently rounded height fractions could
leave the right arm disconnected (including the 39px cactus). Limb widths are
now even and at least four logical pixels: the smallest stem retains two
samples at either phase of the half-resolution scenery grid. This changes
cached cactus geometry only, adding no per-frame filter or allocation. Tests
check connected silhouettes at full and half resolution for heights 39–103,
all sampling phases, and constant sampled small-stem thickness.
