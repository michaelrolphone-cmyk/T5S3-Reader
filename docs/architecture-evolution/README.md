# RiscRTE architecture evolution

Reviewed 2026-10-01 UTC. **Analysis and recommendations only; no implementation approval.**

## Project goals and basis

The owner has **four equal parallel goals**: expressive fast e-paper/apps; the smallest foundational core composing the existing UX from packages; CrossPoint feature parity through apps/drivers; and reliable daily mobile-device/workbench use, including the desk clock, serial/USB/programming tools and games.

Capability-based portability across ESP32 hardware, with future monochromatic reflective LCD and an ESP32 AMOLED smartwatch, is an important part of this vision, not its entirety. A longer-term consumer ecosystem is an aspiration, not permission to add marketplace, onboarding or signing scope. Exact future hardware, memory/input/refresh requirements, CrossPoint target revision and app build contract remain unselected. One source/contract architecture need not mean one firmware or ELF binary across CPU/ABI ports.

The owner's **U1–U4 specifications are the foundation**, including their stream-first order, real ELF hardware ownership, shared chip/rail rules, optional compiled GUI in U3 and one-image proof in U4. They were read in full at master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`, then reconciled with the later active-U1 sequencing/U3/USB-remediation amendments at `8d8f2472fa91b6e5797daa9f03f997ae097b48fa`, including explicit Wi-Fi provider-boundary work. This pass reread the full platform, hardware boundary, sequence and U1–U4 specifications at `5466d93ca6916342d304af10d681208db2566de6` and confirmed all seven unchanged at `64d19d645720cba5c207f67c5d7fa1a031a07c0a`. A spec is intended design, not evidence it is implemented. Separate package-repository preparation does not itself advance milestone acceptance.

## Ranked recommendations for the next evidence

The owner's developer-usable **1.0 beta** target is end of next week (planning date **2026-10-09**, not an independently confirmed release date or a firmware-version downgrade). Rank the next useful evidence, not the four goals or milestone order:

1. **R1 — Close one recoverable bootstrap path, with explicit storage/memory limits.** The CAM now executes installed clock/ZIP ELFs through the production graph and stream/resource lifecycle in two successful cycles [E21](evidence.md#e21-cam-composite-execution-and-single-read-verification-delta). Build on that composite witness: connect first package bytes, ordinary install/update, last-good recovery and exclusive handoff on one declared configuration. Do not repeat provider-execution proof or equate it with complete bootstrap closure. [Shared SD/LoRa fault policy](recovery-contract.md) is unresolved: an outer timeout cannot abort lower SPI work, and safe refusal until reboot differs from in-session recovery. No-SD and zero-PSRAM configurations remain separate unproven contracts, not automatic U1 additions.
2. **R4 — Demonstrate one clean external developer round trip.** Pinned independent builds and Drivers' byte/mode-aware source parity are real progress. The higher-value missing evidence is one reproducible build → canonical package → ordinary install → launch → update → recover path on a declared runtime/storage/memory target. Reuse the existing U1 engine; this applies U2's design, not a new developer portal or installer.
3. **R3 — Preserve expressive data paths and daily workflows.** Retain owned buffers, overlap and no-copy paths. Reader #220 and GameBoy #25 are green at their pinned heads, but still constrain geometry; accepted flip is not physical settling. Evaluate memory, input/return-to-Home, clock wake and tool availability alongside throughput.
4. **R2 — Retain a coherent reader/product boundary; defer wholesale extraction.** Keep the pinned CrossPoint feature map and actual book/state/transfer behavior. Compiled-but-optional GUI is still the simpler correct U3 design; a small external app witness can disprove developer readiness sooner than a complete reader rewrite.

IDs are retained from the prior review; R4 is newly promoted as a concrete evidence gap, **not a new roadmap requirement**. U1 remains solely PR #96; its receipt/inventory work must account for raw legacy writable imports without breaking compatibility apps. No additional implementation writer or hardware test is created here. Daily T5S3 testing is explicitly deferred. [Current evidence and limits](evidence.md#e16-october-1-current-checkpoints-and-evidence-boundaries) distinguish diagnostic, build, package and product proof.

## Read the set

- [Foundation and alternatives](foundation.md): minimum boot/runtime responsibilities, current coupling, recovery choices, product boundary and performance constraints
- [Shared-bus recovery decision](recovery-contract.md): retained behavior, conditional fail-closed ownership and validated abort alternatives
- [Recommendations](proposals.md): alternatives, ownership/lifecycle, budgets, staged evidence and falsification conditions
- [CrossPoint comparison](crosspoint-parity.md): pinned upstream release comparison, source-backed gap candidates and app-vs-behavior compatibility
- [Evidence register](evidence.md): immutable source revisions, facts versus inference, roadmap authority and test limits
- [Revision decisions](decisions.md): re-ranking and retired ideas, including why another package-manager/lifecycle rewrite is not supported
- [Research queue](research-queue.md): the few unresolved facts that could change the recommendations

## Scope and verification

This research remains below [the platform specification](../RISCRTE_PLATFORM_SPEC.md) and its authoritative child contracts. It does not authorize runtime, build, configuration, manifest or version edits, change milestone gates, merge, release, flash or claim hardware success. U3 keeps GUI compiled but optional; full product/UI extraction needs separately scoped work.

Historical findings remain timestamped in [evidence](evidence.md). Current pinned checkpoints are U1 `730d0773`, Reader display `0987497c`, GameBoy `9610cb45` and Drivers `c6c00d6`. No runtime test, serial/device operation, firmware release or milestone acceptance occurred in this documentation pass. Keep three to five active ideas; retire weaker assumptions instead of accumulating scope.
