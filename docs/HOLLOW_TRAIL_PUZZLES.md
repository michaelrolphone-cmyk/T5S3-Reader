# Hollow Trail: discovery and mechanical deduction (1.1.5)

This is the current implementation design. **Full puzzle spoilers follow.**
The narrative's competing accounts and final cabinet decision are unchanged.

## Player experience

Discover physical traces and documents along the route. Each contributes a
constraint, a mapping, or an operating fact. At the exit, manipulate the actual
mechanism, observe its response, revise the hypothesis, and operate its release.
There is no automatic end-of-level solution text or chapter-recap answer sheet.
The apparatus reports physical consequences: a shorted crossover, a full siding,
unequal pressure, a heavy arm, fogged glass, or a cold return losing pressure.
It does not announce the next correct input.

Three stations operate each apparatus. A fourth **RUN** lever tests the result;
it is **PUMP** at the lock and **WIRE** at the ridge. All use the existing A/Confirm
interaction. Final-chapter signal controls and the cabinet decision are retained.
The rail controls first couple the rear wagon on the selected road, then move it
to the second selected road. Selecting the same road again uncouples it. A full
siding rejects the move and leaves the wagon available for another destination.

Discoveries supply knowledge, not arbitrary inventory permissions. Someone who
already understands the mechanism may solve it without ticking every journal
entry. Missing an optional document cannot create an invisible locked-gate flag.
There are no timed solutions, destructive mistakes, randomized codes or required
restarts. Partial arrangements, venting, priming and signal isolation survive a
checkpoint respawn. Fresh chapter/debug replay resets them.

## Evidence chains and deductions

| Chapter | Separate discoveries | Deduction and application |
| --- | --- | --- |
| Forest | Thread: first socket opens toward the road. Register: second points down and each clutch turns its neighbour. Wooden bell: third opens back toward the forest. | Reconstruct the plated-over frame from three rubbings, then solve coupled shaft rotations. Setting one cam can disturb another. Run the mill only when all fit. |
| City | Window sketch maps the two outer feeds. Burnt fuse identifies the first bank's damaged upper crossover. Watch log identifies the independent heater return. | Trace the complete permutation while avoiding a physically shorted route. Two otherwise equivalent permutations are not electrically equivalent. |
| Oil Fields | Lantern tag establishes equal watches. Stove plate establishes full/empty transfers without a smaller measure. Fuel can supplies the 8/5/3 capacities. | Find a transfer sequence that separates the eight-unit supply into equal watch vessels and leaves the small measure empty. No fuel can be invented or discarded. |
| Rail Yard | Torn ticket identifies passenger, fuel and brake wagons. Switchman map identifies the downhill departure direction and 3/2/1 road capacities. Letter places passengers between brake and fuel. | Infer the departure order from cargo relationships and grade direction, then rearrange the train using restricted storage and access only to each road's rear wagon. Restore all cars to the main road before dispatch. |
| Marsh | Token identifies the ferry's lower chamber and bank heights zero/four. Knots establish four measures shared by both chambers and the float-routed pump. Watermarked log establishes the equal-pressure middle gate. | Equalize two chambers without an external water source, transfer the ferry, isolate the chambers again, then use the remaining water to reach the upper bank. Boat location changes the pump's initial destination. |
| Quarry | Stone pouch gives six equal loads. Brake notice requires weight on both arms and the brake. Torn note gives the 3:2 arm lengths. | Infer the unequal masses needed for equal torque, retain brake weight, and allocate a conserved stock through the return path. The arm's visible tilt provides physical feedback. |
| Gardens | Seed packet shows condensation over the lower receiver and a latching vent. Garden label explains reflecting, absorbing and clear faces. Drawing distinguishes the upper vent path from the lower latch path. | First route light straight through the final frame to open the vent. Then reconfigure the same optics toward the lower receiver. A beam reaching the lower glass before venting cannot release the latch. |
| Dam | Gauge distinguishes triangular cold-start marks from circular running marks. Garden pipe requires three units to fill, then one to maintain heat. Ridge circuit establishes the common six-unit stock and the bypass's priming loss. | Establish a cold-start allocation with the ridge isolated, prime the return, then redistribute for all three running demands. A correct running allocation on an unprimed system still fails. |
| High Pass | Scarf identifies the first post and reading direction. Shelter gives the middle two heights. Tin identifies the final height and the inward echo circuit. | Reconstruct the height-to-chime sequence and diagnose why a correct sequence feeds back into itself. Isolate the inward wire while preserving the outgoing path, then send the sequence. |
| Last Light | Lamp and radio card recall the established private signal. The diagram and testimony explain the original/live-pen interlock. | Send the familiar signal, then decide which physical face of the evidence to expose. This remains a narrative judgement rather than another newly introduced combination. |

Discoveries are grounded objects: thread caught on bark, a suspended wooden
bell attached to a branch, documents on worn furniture, fuel containers and
knotted mooring rope. There are no hovering clue plaques or flags. The distinct
rubbings, diagrams and operating facts are preserved in the collected document
prose, so removing world-space diagrams does not remove a necessary clue.

## Implemented state and consequences

- **Mill:** three coupled quarter-turn shafts. The final frame hides its socket
  orientation under plates; nearby discovery rubbings retain that information.
- **Relay:** bypass/upper-pair/lower-pair contacts. A first-bank upper crossover
  shorts even if the resulting endpoint permutation would otherwise be correct.
- **Measures:** capacity-limited transfers preserve all eight units. The release
  tests vessel contents instead of implicitly opening during an intermediate move.
- **Rail yard:** three wagons and siding capacities 3/2/1. Moves preserve identity
  and order, remove only the rear wagon, and reject full or empty selections.
  The departure direction makes the required main-road order brake/people/fuel.
- **Lock:** two water quantities sum to four; three gates and the ferry's chamber
  are independent state. Pumping requires closed gates. The middle gate requires
  equal levels and moves the ferry across. The pump rocker reverses at its end
  stop, so overshooting equality is recoverable without discarding water.
- **Hoist:** all six weights start on the brake. Controls transfer one to the next
  tray around the return, preserving stock. Release requires equal torque and
  nonzero weight on both arms and brake; it cannot accept an empty balanced beam.
- **Heliostat:** the shared bounded ray trace has two receivers. RUN exposes the
  receiver to the beam. Venting latches; later mirror changes do not reseal it.
- **Distributor:** the cold phase requires 3/3/0; running requires at least 2/1/3
  from the same six-unit stock. Priming changes the physical operating phase,
  not an evidence counter. The gauges carry both triangular and circular marks.
- **Ridge:** a connected return corrupts input. The wire isolator clears sequence
  progress and toggles only the inward circuit. Correct transmission latches the
  crossing. The final cabinet's separate decision logic is unaffected.

The standard gate opening animation and physical boundary remain in force after
success. No production solver, search, per-frame allocation or new full-screen
rendering pass is added. State sizes and ray-tracing loops are bounded.

## Development verification

The host test enumerates reachable states for each chapter's actual transition
function and verifies every state can reach a solved state. Feedback text and
render-only flags are excluded from that graph; this checks recovery from legal
experiments, not just one known solution. Separate invariants check conserved
resources, siding capacity/selection, pressure rejection, premature lower-beam
activation, cold-start rejection and the scorched circuit path.

Independent witness input sequences walk the actual ten maps, collect all thirty
evidence records, operate the apparatus controls and complete without deaths.
Rendering uses immutable snapshots. The development build and sanitizer results
are recorded in the PR; these do not establish physical display performance.
