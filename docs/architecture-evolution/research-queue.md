# Focused research queue

These questions refine the existing roadmap; they do not authorize implementation, hardware operations or automatic milestone advancement.

## 1 Bootstrap and recovery facts

For the actual Pro and U3 CAM configuration, identify the first accessible package source, its own power/pin/storage dependencies, population/update route, last-good recovery path and exclusive boot-to-runtime controller handoff. Determine which current board/SD calls are truly required bootstrap versus normal product behavior, and how a future packaged clock preserves the current no-SD/no-full-UI timer-resume path. Verify whether U1's permanent generation-bound integrity receipt/invalidation and coherent inventory work is implemented beyond the intentional hash bypass.

**Evidence that changes R1:** a real dependency graph and linked/runtime witness; failure to read a provider without that same provider; overlap of bootstrap/runtime ownership; or a recovery path lost during ordinary updates. Missing display/media must not require GUI to diagnose/recover.

## 2 CrossPoint target and product boundary

Select the intended original/upstream revision and clarify the actual app source/build interface. Use the [comparison](crosspoint-parity.md) to agree on applicable user-visible features and persisted data. Trace one reading flow's dependencies through reader, settings/font/content state, storage, display and input; record product-specific firmware exports still needed by the candidate app boundary.

**Evidence that changes R2:** a reproducible upstream app/port with preserved behavior and small stable adaptation; or proof that the adapter merely relocates an entry point while retaining product logic in core. Do not equate USB host storage with USB-device drive mode, runtime TTF with build font assets, or source presence with full parity.

## 3 Expressive rendering and daily-work regression budget

Establish exact-scene/quality baseline for fast e-paper before moving a boundary. Reuse current render/pack/copy/wait/input and scan-stage metrics. Attribute peak memory to retained images, relocation, provider frames/state/DMA and quarantine. Include a daily serial/programmer/USB workflow, desk-clock sleep/wake and return from game to Home/reader; determine acceptable disruption with the owner when implementation is scoped.

**Evidence that changes R3:** actual device timing/power/working-set regressions or improvements, copy counts and bounded cancellation; host-only FPS or changing visual quality cannot settle the question. Preserve optimized data paths while generic control manages ownership.

## 4 First non-e-paper provider facts

When actual reflective LCD or watch hardware is selected, verify CPU/ABI, memory, bootstrap, power/input, geometry, stride/color layout, damage/refresh behavior and completion semantics. Check whether the existing display SDK can describe them before extending it. Keep input independent from display and map rotation/safe layout consistently.

**Decision:** introduce only a missing required semantic, not a universal compositor or guessed watch stack. Maintain optional high-performance e-paper behavior and clear unsupported-feature refusal.

## 5 Existing contract issues and conditional reopening

Track PR #220's scan-start/completion and void-stop/quiescence issues without repeatedly reporting resolved defects. Witness the U1 old/new CDC endpoint compatibility seam with pinned packages/runtime. Reopen a standalone package-transaction proposal only on concrete loss-of-generation, mapped-replacement, unbounded recovery or required upgrade evidence that existing safeguards do not handle.

## Maintenance rule

Recheck master/PR state and immutable source revisions. Never edit a closed PR. Keep prior facts and limitations, mark superseded rankings, and publish only meaningful analysis changes. Separate roadmap requirements, implementation evidence, device results and owner acceptance. No code, config, build, manifest, version, merge or release work belongs to this document task.
