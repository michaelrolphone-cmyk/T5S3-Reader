# Hollow Trail scene direction — 1.1.26

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
