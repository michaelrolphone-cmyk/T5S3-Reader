# Quarry hoist descent

The original solved quarry exit now earns a physical cage descent before returning to the exact existing glasshouse spawn. The source balance solution remains 2/1/3 with one block on the brake. The live destination is frozen throughout the view.

The actor shifts one foot off the new rough board as the cage tilts, takes the actual brake cord in both hands and eases it down. The pale wall, damp rooted seams and a fern with a curled new frond pass the cage. The upper landing stays with the source balance controls; the lower landing rises into contact. Hands release after settling and the actor walks onto a gallery whose floor uses the destination terrain. Chisel marks and the water thread follow that same surface.

Pause, archive, controller fault/recovery, cancel, neutral-input rearming and host exit retain the original destination. The completed view holds until continued. Debug level selection does not earn it. Empty-cage trial cycles, the scarf-covered wheel grip, returning boot/look upward and a continuous gallery-to-glasshouse reveal remain separate gaps. Whole-book parity is unfinished.

[Four actual comparisons](images/hollow-trail-hoist/README.md) compare `95a32547` → `5ee385b0`. The tested source is published in PR414; these files preserve its captured visual evidence.

## Verification

Focused ASan/UBSan tests follow the real quarry route and app-input handoff, check shared floor and cord contacts at every tick, bound limb reach, preserve the exact destination and exercise HID/XInput pause/archive/fault/cancel/retry/neutral recovery and host exit. Both raster modes render deterministically. Full native aggregate passes with ASan/UBSan. C++ math and all 30 renderer references pass unchanged.

S3 build, ELF structure and package checks pass. ELF: 367756 bytes; SHA-256 `8beb1a8456ef4861554f367ab4b7f7082a0f711ad46c4972fa9971c894b472cc`. Cumulative app version 1.1.48 → 1.1.49; minimum firmware 1.3.37.
