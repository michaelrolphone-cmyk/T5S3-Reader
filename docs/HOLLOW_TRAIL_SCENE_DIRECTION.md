# Hollow Trail scene direction — 1.1.27

All land chapters now begin from 1.5x intimate framing; the boat chapter keeps
its previous framing and movement. Existing vista envelopes remain, with new
wide passages in the oil fields (X 950–1470) and glass gardens (X 1230–1750).
The underlying fixed-step physics, jump arc and deliberate climb entry remain
as specified in the 1.1.26 record below. Load-triggered landscape movement is
removed; stones can fill physical hollows and crates remain movable solids.

| Chapter | Distant / middle / near camera travel | Composition |
| --- | --- | --- |
| 3: Oil fields | 15 / 33 / 56% | Eroded ridges, fixed pumps, storage tanks |
| 4: Railyard | 17 / 36 / 60% | Earth cuttings, sheds, existing grounded train |
| 6: Quarry | 14 / 32 / 57% | Stratified rock faces, sparse lifting cranes |
| 7: Glass gardens | 16 / 35 / 59% | Curved glasshouse ribs and planted banks |
| 8: Dam | 13 / 31 / 54% | Reservoir valley behind the existing dam and spillways |
| 9: High pass | 12 / 29 / 53% | Angular alpine ridges, snow facets, bolted climbing lines |
| 10: Tower | 15 / 34 / 58% | Hillside settlement beneath one radio mast |

Each composition has a fixed world-space light opening and two sparse thematic
observer masses at 125% camera travel. Shared cached frame storage is keyed by
chapter, horizontal/vertical camera position and projection scale. Background
rasterization, landmark counts and contour tessellation are bounded and service
input checkpoints. The original boat pipeline is unchanged. The nine level
1/2/boat scene and packed-frame reference pairs remain unchanged by this
seven-chapter extension. Device FPS is not inferred from host checks.

## 1.1.26 implementation record

Walking pace is now an authored scene parameter alongside mood, vista and
weather. Close passages should feel like accompanying someone on foot; a
clearing should make that same person feel small inside a large landscape.

| Movement / framing | Previous | This revision |
| --- | --- | --- |
| Ordinary walking | 78 world units/sec | 31–47, authored by chapter and position |
| Forest opening walk | 78 | About 33 |
| Tree / ladder climb | 62.5 world units/sec | 23–35, following scene pace |
| Grounded jump rise | About 75 world units | About 37 |
| Grounded jump duration | About 1.3 seconds held | About 0.83 seconds, tap or hold |
| Pull over a ledge | 0.51 seconds | 1.02 seconds |
| Close forest geometry | 1.0x | 1.5x |
| Full forest vista | 0.6875x | 0.6875x |

These are simulation values, not measured display frame rates. Wind and slopes
can affect displacement. Rowing speed and boat physics are unchanged.

## Authoring and behavior

`Apps/hollow_trail_direction.inc` holds five pace keys per chapter, at world
X = 0, 800, 1600, 2400, 3200. Their interpolation gives a gradual walking
rhythm: deliberate arrivals, broader movement through exposed spaces, slower
returns to the final evidence. Active weather reduces pace slightly; observing
a story landmark slows it further. The resulting target stays in 256–384 Q8
units per fixed 32ms step and approaches by at most two Q8 units per step.

The same scene envelope controls intimacy. The forest starts at 1.5x, other
land chapters at 1.28125x, while the established boat framing remains at its
original scale. The existing authored vistas reduce intimacy as they widen.
Proximity eases gradually, including during a deliberate landmark observation.
The character, roots, rocks, soil and physical props share the same projection;
this is a closer view of the world, not a separately enlarged avatar. Camera
following places the character closer to the centre and permits a small look
back beyond the starting position. Two widely separated close forest trunks
provide foreground occlusion from the observer's bank. They are visual only.

Walking animation follows actual distance travelled, with a short planted
stride, knee/foot lift, arm swing and a settled idle pose. Tree and ladder
climbing retain their contact poses and deliberate entry. Pull-up animation
keeps its hand-contact sequence but takes twice as long.

Jumping uses a fixed impulse and gravity. Holding B cannot extend a leap or
repeat it; letting go cannot cancel the arc. Pace is held at its takeoff value
while airborne, so crossing a scene cue does not change the jump. Ground
acceleration/braking remains responsive, while airborne directional correction
is lower. Rope releases preserve swing momentum instead of immediately braking
to walking speed. No new automatic tree capture or instruction overlay exists.
Input polling, the fixed simulation step, weather time, rigid bodies, and boat
rowing have not been slowed with a global clock multiplier.

## Routes and compatibility

Shorten ordinary gaps to walking-jump distances and lower selected unassisted
steps across the chapters. Keep ladder, boat, load/plate and rope connections;
bring rope landing banks closer and preserve the forest's continuous contours.
Existing living-tree X positions are explicitly retained when banks move.
The input-only route witness traverses all ten chapters without deaths, using
physical interactions and collecting all evidence. It releases ropes with
forward momentum and uses the revised takeoff positions.

The existing camera A/B modes remain. Zoom off and Both off suppress intimacy
and vista draw scaling, without altering pace, collision or input. No firmware
change is required: Hollow Trail **1.1.25 -> 1.1.26**, minimum firmware **1.3.37**.

## Verification

Host checks cover bounded/smooth pace, opening walking distance, fixed tap/hold
jump trajectory, takeoff pace stability, no held-jump repeat, camera/physics
isolation, all existing tree contacts, and the complete route. Updated scene
hashes cover all chapters/readiness masks; forest close/wide compositions and
chapter arrival frames were visually inspected. The native regression checks pass after updating the intended movement and
frame expectations, rerunning the affected checks and completing the remaining
suite. The S3 app build and structural validation pass; source, manifest,
sidecar and catalog agree on 1.1.26. On-device feel and FPS still need
owner evaluation; host execution does not establish either.


## Level 1 reference-guided silhouettes — 1.1.28

Hollow Trail **1.1.27 -> 1.1.28**, minimum firmware remains **1.3.37**.
The original five living trees and two observer-bank trunks retain their spacing.
No extra climbable branches, terrain platforms, capture gestures or help overlays.
The boat and chapters 2–10 keep their existing renderer references.

Photographs inspected for anatomy (geometry is generalized, not image tracing):
- [Ancient Whiligh oak, Country Life](https://www.countrylife.co.uk/nature/when-is-it-ok-to-fell-a-centuries-old-oak-tree): flared roots, uneven girth, burls, swollen branch junctions and subordinate twisting forks.
- [Weathered basalt, James St. John, Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Spheroidally-weathered_basalt_boulder_(Tertiary;_Escalante_Petrified_Forest_State_Park,_Utah,_USA)_6.jpg): broad weathered shoulders, small chips and discontinuous fracture planes.
- [Forest floor photograph, Ricoh](https://www.ricoh.es/servicios/servicio-compensacion-huella-carbono/): arching fern fronds with paired tapering pinnae and lengthwise grain in fallen wood.

Living/near trunks now use 64 paired sections (130 contour vertices), localized
burls, broken growth-following furrows and knot rings. The five substantial limbs
use 24 curved sections each; trunk hand positions and branch footing interpolate
those same vertices. Fine forklets remain non-colliding scenery. Distant trees
retain softer tones without foreground bark detail, and their movement follows
the ridge supporting them.

Existing rocks have 28-point weathered outlines; the movable stone has a 24-point
rounded contour close to its physical radius. Leaves use pointed oval outlines;
ferns have arched segmented fronds and paired pinnae. Root flares have knuckles
and restrained seams. The fallen snag uses 32 sections with bark plates, grain
and scars, retaining its existing bend, rotation and contact profile. Evidence
stumps have irregular sides and splintered tops. Detail is added to existing
objects, without increasing the number of trees, rocks or ground-detail patches.

Bounds checks discard invisible crowns, leaves and branch/twig geometry before
raster work. No heap allocation or full-frame buffer was added. Host moving-camera
CPU render cost measured about 0.50 ms versus 0.40 ms at the merge base; settled
frames about 0.31 ms versus 0.24 ms (800 frames each, same compiler and host).
These are CPU comparisons, not device FPS or display timing.

Verification: original-tree input/contact checks, natural forest interactions,
chapter cache transitions, cache/full-render equivalence, complete route,
scene-direction physics and C++ math/renderer integration pass. Five close/wide
forest views and an isolated canopy were inspected. Only the three forest golden
frame pairs change; other 27 references remain byte-identical. S3 app structural
validation and source/sidecar/catalog version/hash agreement are checked before
publication. No hardware FPS measurement or release qualification is claimed.


### Ground-contact corrections in the same update

Reproduced both reported penetrations against the pre-fix traversal code:
walking beside the stone after rolling it into the forest hollow embedded the
feet at x=313 (feet 248, soil 247); descending the left side of a tree embedded
the feet at x=-16 (feet 196.578, soil 196). Regression tests abort on the first
occupied soil row rather than waiting for a death/respawn to hide the error.

A final bounded terrain reconciliation now runs after attachment motion, prop
sideways corrections and ladder entry. It resolves against the same surface,
underside, socket pieces and eroded wall bounds as drawing/normal collision.
Trees stop on actual soil, not their buried drawing base. Higher terrain takes
precedence over an occluded branch. Ledge climbing raises the feet clear before
moving over the edge and uses the actual surface height. The active ladder's
upper passage remains traversable; the lower bank is solid. Real ravines and
undercuts remain open, and overhead decks do not pull a character onto the roof.

The dedicated ground test covers the rolled-in stone from both sides, walking,
jumping and retreating; descent from both sides of all 16 existing trees;
walk/jump transitions across every chapter's terrain; an occluded branch; and
an actual empty ravine. It is wired into the native-app CI suite. Existing tree
checks now start on soil rather than buried drawing bases and test 78 exposed
limb contacts; the one limb inside a higher bank is covered by the ground test.
Legacy ladder tests that deliberately started inside the lower bank now require
recovery onto that bank. The ledge fixture uses exposed cliffs instead of a
parcel seam within continuous ground.

Final local verification: ground integrity **25,296 poses**, 78 exposed branch
contacts, forest mechanics/cache transitions, controls, scene-direction tests,
renderer references and C++ math integration pass; ground/tree/forest/control/
direction checks used ASan+UBSan (leak scanning disabled in the sandbox). The
complete ten-chapter route, all 30 evidence items and loop pass without deaths.
S3 ELF structural validation passes; source, sidecar and catalog agree on
1.1.28 and SHA-256 `13195385c5a6b16fc4391e78de5b2e073046418055125d30c23e7d1ff72a39c6`.


## Caption clearance and rope attachment — 1.1.29

Hollow Trail **1.1.28 -> 1.1.29**; minimum firmware stays **1.3.37**.
The frozen render camera now reserves room above the narrative band (y=232):
feet plus supporting terrain, or the entire boat hull and reflection, stay at
or above y=211 before the text is drawn. The constraint accounts for scene
scale and the camera's rotation/breathing transform, including every A/B mode.
It adjusts the world camera rather than moving the character relative to the
terrain or hiding the text. Fixed-step movement speeds and jump pacing remain.

Approaching a rope eases out the close camera cue. While attached, wider
framing includes the support as well as the hands/feet; breathing widens and
then eases back after release. The baseline close scale remains elsewhere.
Dam and grotto boat views, unloaded rope and loaded swinging-rope scenes were
rendered with the actual narrative overlay and visually inspected.

Ropes have a continuous dark body and diagonal strand highlights anchored to
length, so the weave moves with the rope. Two wraps, a hitch and a short frayed
tail replace the black/white anchor disk. A bounded damped spring moves the
physical attachment toward the player's load and arc; wood can yield up to five
world pixels, metal supports up to two. The branch/crossbar follows the pin,
the forest limb's footing shares its bent geometry, and unloading settles back
to rest. Previous pin coordinates track actual motion for rope-release velocity.
There are no additional scenery objects, frame buffers or per-frame allocations.

Verification: 1,360 camera-clearance cases across chapters, camera modes,
rotations and scales; loaded support bounds, continuous pin motion, unloaded
return and branch-footing agreement (ASan+UBSan). The ground regressions
(25,296 poses), 78 branch contacts, forest cache transitions, scene-direction
physics, complete ten-chapter route and C++ math integration pass. The three
boat chapter renderer references and all ten grotto frame hashes remain exact;
the other chapter references intentionally reflect higher framing/new ropes.
S3 ELF structural validation and source/sidecar/catalog version/hash agreement
pass. On-device appearance and performance have not been measured.


### Sustained effort and gravity-driven toppling (same unreleased 1.1.29)

A engages the standing snag; pushing toward it is now required for 48 fixed
steps (1.536 seconds). The character moves into a braced stance, plants the rear
foot, bends the knees and presses both hands against the bark. Effort shifts the
shoulders forward with a restrained breathing/strain cycle. A small increasing
lean shows the root resistance before release. Idle input loses effort; backing
away, releasing the grip or jumping cancels the action. No new prompt/meter.

Once the roots give way, the character withdraws the hands in a brief follow-
through. The trunk integrates angular speed with torque increasing with the
sine of its lean. It starts nearly still and accelerates until the far-bank
contact; it becomes walkable only after landing. Partial effort/falls reset on
respawn, while an already landed crossing persists. The resting and landed
geometry remains unchanged, including existing renderer references.

The forest regression now checks idle input, interrupted effort, the full push
threshold, backing away, jumping away, monotonic angle/increasing angular speed,
no early footing and respawn behavior under ASan+UBSan. The input-only complete
route pushes and waits for the actual fall. Ground, tree, framing, controls,
renderer, complete-route and C++ math checks pass. Pushing/follow-through poses
were visually inspected. S3 app build and ELF structural validation pass; the
source, sidecar and catalog remain 1.1.29 for this cumulative unmerged PR.

### Solid snag and speed-matched gait (same unreleased 1.1.29)

The standing/falling snag now blocks the player's body from either side. Its
32 short collision sections use the same bend, taper, bark outline and angle
as the drawing. Swept horizontal contact stops airborne/high-speed approaches;
collision is applied after attachments/props, before ground recovery. The
landed tree retains its existing crossing surface. Walking into the trunk leaves
the player within push interaction range, without triggering the fall.

Free grounded movement blends from walking into a running gait over actual Q8
speeds 304–336. Running uses a longer stride, forward torso lean, higher knee
recovery, brief flight and bent-arm swing. Cadence follows distance; speed cues
remain unchanged. Idle, jumping, climbing, pushing and rowing retain their own
poses. Slow/brisk/run contact sheets were inspected.

Walking/jumping into the snag, both-sided fast approaches at multiple heights,
other-chapter isolation, sustained pushing and landed crossing pass. Ground
integrity (25,296 poses), 78 branch contacts, scene pace/gait selection, complete
ten-chapter route, forest ASan+UBSan and C++ integration checks pass.

## Articulated character — 1.1.30

Hollow Trail **1.1.29 -> 1.1.30**, minimum firmware **1.3.37**. Branched
from master after PR #320 merged. All activities now share a proportioned
silhouette: profiled crown/brow/nose/chin, neck, shaped jacket with a waist and
hem, longer legs, ankle/heel/toe contours and subdued close-view clothing seams.
The oversized head disks and white eye cutouts are removed. Rear limbs are
slightly quieter than the near limbs so overlapping poses remain readable.

A common pose builder places hips, shoulders and contacts; integer two-bone
joint construction supplies knees/elbows. Walking uses a planted stance that
cancels world displacement and an elevated recovery arc. Running blends knee
lift, torso lean, arm carriage and brief flight with actual speed. Foot heights
follow nearby soil slopes. Ascent tucks the legs and lifts the arms; descent
extends them for landing, whose compression follows the existing impact state.
No physics speeds, collision sizes, terrain, camera direction or controls change.

Climbing contacts follow trunk contour or ladder rungs. Ledge hands use the
actual lip height. Rope hands follow the simulated grip with trailing feet;
rowing palms retain the shared oar stroke. Stone/crate and tree pushing retain
their physical contact points, brace and effort/release states. Fixed small
pose structs, bounded contact queries and integer math add no allocations,
textures or frame buffers.

Walk/run/jump contact sheets and actual forest walking, running, tree pushing,
climbing and seated boat scenes were visually inspected. A focused character
regression covers walking leg lengths, planted stance in both directions,
idle stability, jump phase changes and exact oar contact. Renderer full-frame
references are updated for the intentional character changes; the independent
300-view grotto geometry and cache pixel-equivalence comparisons remain intact.
S3 build/ELF validation and source/sidecar/catalog version/hash agreement pass.

### Dam waterfall and foreground detail (same unreleased 1.1.30)

The reservoir wall uses a separate 88% horizontal parallax plane, anchored near
the boat basin, between the distant valley and the 125% observer foreground.
Buttresses have shaded side faces, wet edges, concrete courses and restrained
fractures; the crest carries a continuous parapet/railing. Spillways have dark
recesses and projecting lips rather than bright rectangles painted on the wall.

Each waterfall has 32 irregular edge sections, a luminous central sheet,
translucent-looking side veils, six folded ribbons and time-advected packets.
Packet position is quadratic in age, so streaks accelerate and stretch through
the fall. The broad central discharge meets the boat basin at its water level;
outer chutes continue into the lower gorge behind the banks. Three feathered
mist volumes per visible discharge blend with the wall/water, followed by broken
expanding ripples. All draw before nearer terrain, boat and character. No
random per-frame noise, allocations or additional frame buffers are used;
visible bounds cull individual chutes and clip the two-pixel mist work.

The two existing foreground sites retain their spacing. Fractured abutments
use forty contour sections with small edge seams; return pipes have highlights,
anchored brackets, bolted collars and a small tuft of bent grass at a damp seam.
This adds depth/detail without changing terrain, traversal or scene pace.

Approach, boat basin, far-bank and close foreground views were rendered and
visually inspected. Only dam full-frame renderer references change; all other
chapters, including the grotto boat scene, retain their reference hashes.
S3 app build and structural validation pass; source/sidecar/catalog agree on
1.1.30 and SHA-256 `84c9ebd3e3197eaef1e5667ca2d3635a4bb1364bbbf6041c15a56716b9989cb8`.
Device appearance and FPS have not been measured.

### Photo-guided detail in the remaining six chapters (unreleased 1.1.30)

Scope: oil fields (2), railway (3), quarry (5), glasshouse (6), mountains (8)
and settlement (9), using zero-based chapter numbers. Forest, city, grotto boat
and dam raster references remain unchanged. Existing object sites, counts,
terrain, movement and physical branch axes remain in place.

Reference photographs reviewed for general form, not copied into game assets:
- NPS mature saguaro: rounded stem, unequal upright arms, curved elbows and ribs:
  https://home.nps.gov/sagu/learn/nature/saguaro.htm
- Library of Congress pumpjack, Carol Highsmith: horsehead, walking beam,
  bearing, pitman and counterweight:
  https://www.loc.gov/pictures/item/2020743415/
- Library of Congress side-view steam locomotive: boiler bands, domes,
  running gear, spoked wheels and handrail:
  https://www.loc.gov/item/2022647579/
- USGS Thunder Hole granite, Alex Demas: stepped fracture planes, chipped
  shoulders and connected joints:
  https://www.usgs.gov/media/images/granite-outcropping-thunder-hole
- Library of Congress White House conservatory: closely spaced glazing ribs,
  long pane divisions and layered broad leaves:
  https://www.loc.gov/pictures/item/96512664/
- NPS Great Basin bristlecone: irregular trunk, exposed grain, sparse twigs
  and needle clusters:
  https://home.nps.gov/grba/planyourvisit/identifying-bristlecone-pines.htm
- Library of Congress Goldfield street: overhanging eaves, recessed sash
  windows, porch edges and weatherboard facades:
  https://www.loc.gov/pictures/item/95508788/

Implementation: 36-vertex ground stones with joined fracture planes and snow
caps; 48-section close rock contours; 24-section rounded cactus stems with
12-section curved unequal arms and ribs; 40-section bark/knots on existing
alpine/glasshouse trunks, keeping branch footing geometry; 12-section leaves
and 40-section glazing arches. Pumpjacks gain crank/pitman/bearing details;
quarry cranes gain lattice and hooks; trains gain eight-spoke wheels, valve
rods, boiler bands, steam domes, handrails and carriage panels. Settlement
houses gain eaves, siding, divided sash windows, sills, porches and chimney
caps. Foreground pipe flanges, telegraph insulators and glasshouse rivets/glass
edges receive sparse detail. Material shapes remain bounded and world anchored;
no runtime photographs, textures, allocations or new frame buffers.

Eighteen actual scene views (three per chapter) were inspected alongside the
reference photographs. A follow-up corrected the cactus elbows/tips from
branch-like diagonals to the rounded upright saguaro form. Renderer references
change only in these six chapters. Full route, existing collision/contact
checks and cache equivalence pass; S3 build and ELF structural validation pass.
Device appearance/FPS are not claimed.
