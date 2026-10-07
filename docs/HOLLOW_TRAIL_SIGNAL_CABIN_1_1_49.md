# Signal cabin: corrected map and paper dog

The original Chapter IV map evidence now opens a physical scene at its existing
rail-route location, world X=1855 on support 4. The narrow three-road window,
numbered lever row and wrapped grip come from the novella. The shunting solution,
terrain, evidence identity, archive/read guard and surrounding route are unchanged.

A takes the map with a shared reachable hand contact. L/R tilts it toward the
window, making the erased question's pressure visible. A approaches the middle
lever and unwraps its paper grip. The player can hold the five-legged dog quietly,
then wrap it back; a final A opens the retained map record. The long legs, pierced
eye and twice-traced belly outline follow the passage. No new character or answer
is invented. This player-earned cutscene follows the owner's direction to use
cutscenes where appropriate to tell the story.

Pause, archive, controller loss, cancellation, retry and host exit retain the same
controller conventions. Gameplay freezes throughout. Each pickup has an eight-tick
contact hold; neutral map/paper holds do not churn the render revision.

Source capture: `2b5f32852` → `df9b3eead`, both unreleased 1.1.49.
`scripts/preview_hollow_trail_cabin.py` drives the actual route and real app input,
then captures the production grayscale and packed monochrome renderer. Every
paired raster region is checked against its original pixels. Captures are in
`dist/cabin-before` and `dist/cabin-after`; publication progress is disclosed in PR414.

Focused ASan/UBSan tests cover HID/XInput interruption/retry, all shared arm/leg
contacts, frozen route state, physical pressure-only monochrome change, five legs
and the eye hole. Exact-book prose, C++ math, S3 ELF/package and version checks pass.
Only rail view 3/2 intentionally changes its baseline reference to include the
cabin; the other 29 baseline renderer references remain exact. The full native-app aggregate completed with exit 0 under ASan/UBSan, including
the complete route, ending guards and all 300 original grotto references.

S3 ELF: 387,656 bytes, SHA-256
`0b2d2124822227c680947d8810fd5d639b4032ff5b0d4f3acac458fd60a504c0`.

The under-track bracing/wool sequence remains a distinct unfinished increment.
This room treatment does not claim whole-book completion or device-panel proof.
