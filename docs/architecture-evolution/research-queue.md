# Focused research queue

These questions refine the existing roadmap; they do not authorize implementation, device operations or milestone advancement. IDs below refer to the stable R1–R4 recommendations, not goal weights.

## 1 Close the actual bootstrap and resource contract — R1

First resolve the [shared-bus recovery decision](recovery-contract.md) with the sole U1 owner: accept bounded fail-closed SD/LoRa outage until reboot only if lower polling/locks, raw admission and retained task/resource lifetimes can be proven safe. Otherwise retain explicit limited guarantees or separately scope validated abort, filesystem/cache and radio reinitialization. An outer timeout, task deletion or storage-only flag is insufficient. The current receipt/inventory slices are progress, not proof of lower-media termination [E19].

Use the CAM composite witness as the retained execution baseline: installed clock/ZIP, production graph/stream/resource contexts, negative rights/backpressure and two clean release/reacquisition cycles now pass [E21](evidence.md#e21-cam-composite-execution-and-single-read-verification-delta). The useful next authorized evidence connects cold boot and first package bytes to ordinary install/update/recovery and exclusive handoff in one declared configuration. Do not repeat isolated provider execution, add a second host/manager, or infer the whole path from results at different revisions. Record exact mount/package generations, recoverable last-good state and behavior with removed/corrupt media. Coordinate its generation seam with the sole U1 receipt/inventory owner. Raw writable legacy SD/FS imports mean HalStorage-only invalidation is not enough; resolve ongoing/retained writer uncertainty while preserving compatibility.

Retain the common transaction engine in light of the 40-case pinned LittleFS host witness. Investigate the consumed-core-state/retained-VFS-descriptor close failure rather than retrying blindly; retire the 64-character-name objection for the tested configuration. Host fault coverage is neither a complete backend nor physical qualification, and CAM SDMMC evidence does not qualify T5S3 SPI [E20].

For the no-SD relay and zero-PSRAM Tracker, separately establish package-store population/recovery and executable mapping/peak memory-class feasibility before labeling package execution supported. Do not create a universal flash store or new U1 gate from diagnostic success. Preserve the clock's no-SD/no-full-UI wake path. PSRAM on the relay remains unknown.

**Evidence that changes R1:** a real ordinary-package load/recovery/ownership witness, or a concrete cycle/capacity/memory-class failure that falsifies the retained SD or pre-staged-store alternative. No physical tests are requested by this review.

## 2 One reproducible developer round trip — R4

Use an existing useful external app and actual provider dependencies with pinned runtime/SDK/toolchain/source. Capture clean build, exact imports/ABI, canonical ZIP identity/hash, ordinary install/launch, strictly newer update and failed-update recovery/relaunch preserving user data. Record storage generations and aggregate memory headroom. Retain current Reader sourcing as a baseline until independent source/package delivery is proven; build parity alone is insufficient. Distinguish software fixtures from physical evidence and respect deferred daily T5S3 testing.

**Evidence that changes R4:** another developer can reproduce the complete declared path without hidden checkout state or manual file replacement. A failure identifies the last proven stage; it does not justify a new manager. October 9 beta planning should be assessed against these explicit claims, not inferred from heartbeat counts.

## 3 Expressive rendering and daily-work budgets — R3

Use current display `0987497c` and GameBoy `9610cb45` as updated source/CI baselines, retaining geometry/format limits. Separate buffer acquisition, accepted flip, optical settling and verified quiescence; do not turn optical settling into every-frame backpressure. Review the void-stop contract independently.

At an authorized qualification point, compare identical scenes/quality using render/pack/copy/wait/input and scan metrics; account for provider/candidate/relocation/frame/DMA/quarantine memory. Include serial/programmer use, game exit/Home restoration, reading-state resume and clock sleep/wake. Current daily-device physical testing is deferred, not passed. Host FPS and build success cannot settle latency, power or visual quality.

## 4 Product boundary and selected CrossPoint target — R2

Preserve the [pinned feature comparison](crosspoint-parity.md) while the intended revision/app build contract is selected. Trace book open/render/page/save/exit/resume plus representative transfer/settings and applicable parity gaps. A coherent product bundle with a small reusable interface remains the candidate; compiled-optional GUI stays the U3 baseline. Source/package migration does not prove product code left firmware.

**Evidence that changes R2:** a thin reproducible adaptation preserves actual behavior and persistent state without adding feature-specific firmware calls. Full extraction is not required by U3/U4 or authorized by the beta label.

## 5 Conditional questions only

When a genuine second display target is selected, establish CPU/ABI, storage, memory classes, input, geometry/stride/format and refresh semantics against the existing SDK before extending it. Tracker TFT presence is not a provider witness or permission to migrate it. Reopen transaction/lifecycle rewrites only on concrete failures the existing machinery cannot handle. Verify current CDC compatibility at canonical 0.1.8 rather than treating the old 0.1.7 identity snapshot as current.

## Maintenance rule

Read current source/ownership and PR state; preserve user edits and never edit a closed PR. Keep the ranked set at three to five ideas. Separate roadmap design, build/source checks, actual package/runtime results, physical qualification and owner acceptance. Only docs/architecture-evolution/ belongs to this task.
