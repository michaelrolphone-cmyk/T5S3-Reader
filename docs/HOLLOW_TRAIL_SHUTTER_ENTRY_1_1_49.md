# Hollow Trail 1.1.49: enter through the broken shutter

Chapter I's sentence, “I entered by a broken shutter and found the register on a
sloping desk,” now has a physical transition between the grounded mill approach
and the existing desk. This continues the same cumulative unreleased
**1.1.48 → 1.1.49** update. The whole-book adaptation remains in progress.

## Connected action

The downhill broken shutter occupies X1093–1106, on the existing continuous
terrain. Its wooden sill at Y183 and lintel at Y147 are shared by the renderer
and collision code. The light-casting broken window and barred uphill door
remain separate. The barred door does not open. No platform, new terrain parcel,
cutscene, automatic evidence or story event is added.

Facing the opening, A takes hold; Left/Right moves through it. At normal input,
the complete movement takes 96 physics ticks (3.072 seconds): take jamb contact,
raise each foot onto the sill, cross, lower onto the existing floor, and release.
Neutral input holds the current contacts. Reversing input backs out through the
same positions. A/B cannot drop a suspended character into timber. Ordinary
jump physics still sees the real sill and lintel, including the open aperture.

The articulated pose reuses the existing two-bone arms, legs, coat and raster
primitives. Hands transfer between jambs while feet step from the actual ground
to the sill and back. The near timber is drawn after the character so the contact
reads as a passage through the building. Grounded entry ends at X1111, the desk's
original left grip position. A then grips the desk, and ordinary movement draws
it into the window beam for the existing register/memory interaction.

The desk starts at X1128. Its left travel now stops at that original position:
the player cannot pull it through the shutter or block the return landing. Its
right limit, light interval, hand contact, scrape, evidence and memory remain.
A fresh chapter starts outside. The mill checkpoint is earned only beyond the
sill and restores the character beside the desk. Retrying before completing the
crossing returns to the previous checkpoint; no suspended attachment survives.
Later backtracking uses the same reversible interaction without a one-shot lock.

## Evidence

[Production before/after grayscale and packed monochrome](images/hollow-trail-shutter-entry/README.md)
cover approach, initial contact, sill crossing and the interior handoff. The
baseline is `78d28e27`, whose app source is identical to public commit
`f42a82d71e4df400ed83a346cbd7bba08c812ef4`. After captures use actual A/Right physics
calls; before captures walk through the legacy wall to the comparable location.
They are renderer captures, not generated artwork or hardware measurements.
Each image retains 960×540 original pixels; comparison raster regions are checked
byte-for-byte. Source hashes and actual actor/ground coordinates are recorded.

Regression coverage includes real HID/XInput approach and entry, neutral hold,
backtracking, repeated entry, every approach position, bounded continuous body
motion, hand/foot reach and ground clearance, render-only state preservation,
desk/memory handoff, checkpoint recovery and the input-only ten-chapter route.
The full route still gets all 30 discoveries without deaths or setting gameplay
flags. Only the forest-middle gray/mono reference changes; the other 29 pairs
remain exact. The complete native-app aggregate passed with ASan/UBSan (local leak detection
is disabled for the runner's ptrace limitation). The S3 build, ELF structural
validation, changed-source version guard against `origin/master`, ordinary ZIP
export/validation and source/sidecar/package/catalog/ZIP identity all passed.
Final ELF: **286364 bytes**, SHA-256
`795907f3f76d82343a6ad5001836c032e054a458224dc4b6f9752675f40e3a82`.
No hardware operation is performed.
