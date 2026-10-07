# Hollow Trail 1.1.49: a door without a light

The actual tower exit now continues into Chapter XI instead of automatically
looping back to the forest. This is another slice of the cumulative unreleased
**1.1.48 → 1.1.49** app update; minimum firmware stays **1.3.37**. The whole-book
adaptation remains incomplete, and this increment does not merge or release it.

## Playable ending

The ten original route/puzzle/evidence slots stay intact. A bounded epilogue
state follows the grounded, solved tower exit only after the chosen testimony
has actually been read. The choice, evidence and archive are retained.

- Walk down the far stairs into the ordinary, windowless yard. A near the
  barrel sets down the pantry lamp and earns the 15.36-second rest. Paired drops
  and the moving chair shadow resolve into the same stationary chair.
- Continue through the low passage and down a continuous street, with low
  houses, settled paving/gutter, a parapet tree, and no signal wires or beacons.
- At the door, A lowers the lamp beside the small shoes for a 7.68-second hold.
  The notebook is a deliberate A interaction, not a timed reward.
- The canonical black-handle notebook is the exact Chapter XI passage, with
  paragraph breaks and the existing ASCII typography normalization. The game's
  retained alternate choice records only the live pen it witnessed; it cannot
  claim to have copied the original sealed on the other face. A chapter jump
  cannot manufacture the unread schoolroom memory.
- Only A on the last actually submitted notebook page advances. Cancel and
  reopening preserve the opportunity to read; holding A cannot skip it.
- A 15.36-second grief hold, 7.68-second shoe-righting action and 5.12-second
  ordinary irregular knock lead to a completely held, unanswered frame. No
  automatic replay, revealed figure, new clue, HUD or interaction prompt follows.

Pause, journal, explicit chapter replay and host exit remain available. Replay
is deliberate. Ordinary death/respawn logic cannot erase the final frame.

## Picture and bounds

The epilogue uses one fixed 1.5× world scale, with a caption-safe foot plane,
no camera sway, breathing zoom or dramatic vignette. Camera following belongs
to walking; the yard rest and all door actions hold their framing. The entry
camera keeps the traveler visible from the first step. The final door has no
bright sky slit above it that could act as a destination beacon.

The same shared articulated silhouette and ground function draw the traveler
and support the feet. The body leans over planted feet before touching the
shoe; hand targets stay within the shared 6+6-pixel arm reach, leg targets
within 8+7 pixels. A carried lamp is attached to the hand, then rests beside the
knees. The lamp's pool reaches the worn latch and two shoes while the upper
door stays dark. The ordinary knock has two irregular hand motions, not a
repeating signal or exposure pulse.

Unframed ending frames clear the inherited packing border and submit the full
display height. The first framed replay frame also submits the full height, so
no old tower/ending rows remain on the panel. Real app_main tests cover this
with delayed submissions, one retry, staged packing and direct-packing fallback.

No new raster arena, texture, heap allocation or filesystem operation is
introduced. Scene, pose and clock work have fixed bounds. Background batches,
houses, paving and the local lamp pool call the existing elapsed-time-aware
input/service checkpoints. WAIT does not increment simulation time or scene
revision. Grayscale and packed output remain byte-identical after 10,000
additional attempted movement ticks in the preview and 20,000 in the test.

## Actual evidence

[Before/after frames and regeneration](images/hollow-trail-final-door/README.md)
use the real production app input, simulation, journal, raster and mono packer.
The baseline is the parent integration at **eeafe0ffc7a5baf2c56e77d053d171f43a2b026e**. Both sources start with
an identical reached, read black-choice tower exit; the baseline loops to the
forest, while the new source walks through the ending. All screenshots were
visually inspected. These are host captures, not generated illustrations or
hardware appearance/FPS evidence.

Passed locally:

- ASan/UBSan final-door input/state test: HID and XInput, both choices, eligibility,
  earned pauses, pause/resume, release-edge guards, notebook cancellation and
  last-submitted-page guards, unchanged evidence, static final state, deliberate
  replay, exit, both raster modes, prior-frame independence and shared limb reach
- Existing ending and controller regressions under ASan/UBSan
- Exact Chapter XI notebook prose comparison and bounded body size
- Full ten-chapter physical route with all 30 evidence items and no deaths,
  production renderer references, video pipeline and cooperative render service
- ESP32-S3 app build, loader structural validation, ordinary ZIP export and
  offline package verification. Source, sidecar, compatibility catalog and
  package catalog agree on 1.1.49. Local ELF SHA-256:
  `a983c6d998fdd3a7b43b9869eb14e8955a1e9df31e26d117dc46017f529ec084`

The installed S3 compiler was selected through NATIVE_APP_CC because the default
PlatformIO path was absent. Sanitizer leak scanning was disabled in this runner.
The parent records combined integration/CI results after applying this commit;
this local check does not claim the whole repository aggregate or device
qualification passed. No merge, release or flash was performed.
