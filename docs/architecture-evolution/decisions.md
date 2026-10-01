# Recommendation revisions and retired directions

## 2026-09-30 Initial source-grounded review

**Status:** proposed research conclusions only. No architectural implementation was approved or performed by this review.

### Retain and narrow

- **Display abstraction:** retain the existing semantic surface and adaptive geometry. Prioritize truthful presentation/shutdown semantics before moving physical execution into the provider. Source tracing found scan-start versus presentation-completion and void-stop versus quiescence distinctions. Existing host safeguards narrow the claim: no current use-after-free or hardware failure is established [E5 in evidence](evidence.md#e5-display-adapter-and-backend).
- **Capability compatibility:** retain the current minimum-version-to-exact-binding bridge. Inspection disproved an initial concern that those two matching styles alone implied a resolver bug. The stronger recommendation is a compatibility/provenance contract with mixed-version witnesses, especially the U1 serial endpoint suffix transition [E1, E6](evidence.md).
- **Memory:** keep a separate, lower-ranked acquisition-wide budgeting proposal because provider snapshots, mapped code and quarantine are not charged to the app ledger. Make implementation conditional on useful measurements; a source-derived display buffer sum is not a whole-system peak.

### Retire or defer

1. **New package transaction manager or blanket multi-package atomic upgrade. Retired.** Existing staged publication/recovery, a shared mutation guard, per-package use gates and retained pins after failed unload are substantive counterevidence. No inspected failure establishes that replacing them would improve reliability. Keep targeted recovery/lifetime questions in the research queue; do not elevate architecture by renaming already implemented safeguards [E8](evidence.md#e8-package-publication-and-lifetime-counterevidence).
2. **General provider lifecycle rewrite. Retired.** The graph already owns dependency tables/candidate metadata, generation-qualifies grants and retains dependencies after failed quiescence. The display boundary needs to honor those guarantees, not displace them [E2](evidence.md#e2-existing-provider-lifetime-foundation).
3. **Storage VM as the immediate memory fix. Deferred.** The memory specification itself keeps stacks, data and normal ELF residency in RAM. A new paging/cache subsystem would not cure peak relocation copies, DMA constraints or uncertain teardown; first measure those lifetimes [E3, E4](evidence.md).
4. **General SemVer/SAT capability solver. Deferred.** Selection machinery does not define binary compatibility. Keep bounded existing selection unless a concrete incompatible-family or multi-provider use case requires more [E1](evidence.md#e1-capability-selection-and-abi).
5. **Declare migration repository SDK drift or stale CI. Rejected as current findings.** All 22 overlapping driver SDK headers match the pinned Reader revision. Exact Drivers-head CI succeeds despite older migration prose saying some checks are pending. The gap is durable provenance and future compatibility coverage, not demonstrated current drift [E6, E7](evidence.md).

## 2026-10-01 Clarify the portability goal

The owner's ESP32/downloadable-driver clarification is context for these recommendations, not the whole platform vision. Keep all three priorities and the earlier rejected ideas. Strengthen P1 from a correct e-paper seam to a capability-based behavioral contract: existing SDK metadata is broader than the current MONO1/GRAY2 client, and the adapter ignores presentation options [E9](evidence.md#e9-display-portability-refinement). Recommend negotiation, adaptation or explicit refusal; do not prescribe a new compositor or speculative smartwatch stack. The three parallel near-term goals are expressive fast e-paper rendering, a minimal core preserving existing UX through drivers/apps, and CrossPoint product parity enabling its app. Preserve optional e-paper performance paths; do not equate core reduction with function removal. P2 remains platform-wide and adds a target-revision-specific CrossPoint compatibility witness; P3 becomes explicitly port/resource dependent. Reject the inference that hardware-independent applications require one binary across all CPU/ABI ports.

## Revision discipline

For each later change record date, affected proposal, new immutable evidence, what was contradicted, and whether the ranking or recommendation changed. Keep superseded ideas here with short rationale, not as active TODOs. Remove questions once answered. A new active idea must displace a weaker one or explain why it belongs in the three-to-five-item set.

## 2026-10-01 Reassess after reading the full roadmap

**This revision supersedes the initial ranking.** The earlier review put local completion/ABI/memory issues ahead of the product architecture and did not sufficiently use the U1–U4 specifications. All four and their sequencing, stream, shared-owner, bus, headless and provisioning contracts have now been read in full.

- Promote **boot/recovery dependency closure and exclusive handoff** as R1. Current source still mounts SD and initializes board/display/UI in a coupled order; the roadmap already requires a different U3 driverless boot. The useful analysis is how to close that graph with real bootstrap media and recovery, not inventing headless operation as a new idea.
- Promote **product application ownership and CrossPoint compatibility** as R2. Settings/file-transfer ELFs can call compiled product behavior; the reader is compiled. Separate behavior parity, portable app ownership and CPU/binary compatibility. Use upstream 1.6.5 only as an explicit comparison until the owner selects a target.
- Reframe display completion and memory findings under **high-throughput data-path preservation** as R3. Expressive e-paper is a parallel product goal, not merely a portability demo; retain efficient buffering and measure the actual scene/input/power tradeoff.
- Demote standalone **SDK provenance/independent release** advice: U2 already requires it. The prior sources still expose useful implementation/compatibility evidence, but do not establish a novel top-level direction.
- Keep the previous rejected transaction/lifecycle rewrites rejected. The full roadmap further supports one manager, one stream foundation and one owner per physical chip/rail rather than parallel engines.
- Preserve the four-way constraint: e-paper expression, core reduction without losing UX, CrossPoint parity, and daily device/tool/clock/game usefulness. Core bytes and abstraction elegance alone are not success.
- Do not turn the minimum-core aspiration into an immediate UI-ELF gate: U3 explicitly keeps UI compiled but optional; U4 does not require its extraction. Later product packaging needs a scoped decision, not an invented extra milestone.

**Final reconciliation:** active U1 head `8d8f2472` contains later sequencing/U3/USB-remediation text requiring the Wi-Fi hardware/provider boundary; it overrides the older master exclusion. The earlier `74c7bdfd` contract/CI findings remain historical. R1–R3 are analysis questions, not priority weights: U1 should close with reuse and bounded scope, without subordinating expressive e-paper, minimal core, CrossPoint parity or daily usefulness to endless foundation work.

## 2026-10-01 Beta evidence, actual lab tiers and retained designs

[E16–E18](evidence.md#e16-october-1-current-checkpoints-and-evidence-boundaries) materially change the next evidence to seek:

- **Narrow R1:** keep one recoverable bootstrap closure, but stop treating an SD reader as a universal solution. The actual no-SD and zero-PSRAM boards expose separate storage/executable-memory contracts. Diagnostics and a compile-linked SD harness leave package/ownership/recovery proof open. Retire a default universal flash store; compare pre-staging only where a verified target needs it. No automatic U1 board expansion.
- **Promote R4 ahead of wholesale R2 extraction:** independent builds now exist, and Drivers' byte/mode-aware audit fixes real provenance weakness. The missing discriminating result is a clean useful-package install/update/recovery round trip. This is U2's existing design made concrete for the beta planning target, not a new installer, onboarding program or U1 gate.
- **Retain compiled-optional GUI and current source during migration:** a full reader rewrite is a weaker next step than a small external witness. Preserve R2's product/parity analysis as a later scoped extraction question; neither beta urgency nor repository separation authorizes cutover.
- **Narrow the display completion criticism:** current accepted flip is not a promise of optical settling. Retire any suggestion that every frame must wait for settling; retain the separate truthful stop/lifetime question and no-copy/overlap paths.
- **Reject HalStorage-only receipt confidence:** raw compatible SD/FS writers are counterevidence. U1 already owns conservative invalidation and coherent inventory; do not duplicate it or remove working apps to simplify a cache.
- Keep all four goals equal. October 9 is a planning interpretation, not a release promise or a firmware version change. Daily T5S3 testing remains deferred; no CI/diagnostic result closes product qualification.

Publication discipline: this pass started from open draft #328 at `c65ed014`; the branch had no competing PR comment claim and no other active architecture writer was visible in the checked task inventory. GitHub reported mergeable=true on recheck, so the previously reported conflict required no repair. No master merge or out-of-path change was used.

## 2026-10-01 Challenge recovery claims against lower-level failures

R1 remains first, but [the owner decision](recovery-contract.md) now distinguishes recovery after reboot from safe in-session resumption. Reject outer timeout as transfer cancellation, caller deletion as mutex release, HalStorage-only quarantine as protection against raw clients, and SD-only outage as the T5S3 shared-bus contract: LoRa is affected too. Prefer evaluating a narrow existing-port fail-closed correction only with accepted compatibility limits and proven bounded detection/retained owner lifetime; retain current limited guarantees if that cannot be shown. Controller abort plus filesystem/cache/radio reinitialization is extra scope, not an automatic U1 or U3 rewrite [E19].

Retire the 64-character LittleFS transaction-name blocker for the tested configuration: 77 characters are valid. Forty simulated interruption cases strengthen the retained common transaction design, not physical/backend readiness. Reject blind close retry where VFS retains a descriptor after core file state is consumed; keep durability uncertainty distinct from live-resource retention [E20]. No new ranked proposal, hardware test, package engine or network rewrite is introduced.

Ownership was rechecked against PR #328, the task inventory and current U1 ledger before this documentation pass. The sole U1 implementation owner retains all implementation work, including HTTP idle-body correction. Architecture publication is limited to this directory; the documentation claim is released after exact-commit verification.

## 2026-10-01 Retain CAM composite execution; narrow the remaining proof

[E21](evidence.md#e21-cam-composite-execution-and-single-read-verification-delta) changes R1's starting point: installed clock/ZIP execution now passes through production graph and stream/resource lifecycle on CAM. Retire the blanket current claim that this integration is merely compile-linked; retain its exact scope and avoid repeating those stages as fresh research. The next discriminating result connects boot, ordinary install/update/recovery and exclusive handoff on one declared configuration. No ranking change or new package/stream subsystem is justified. U1's single-read/explicit-verification invalidation improvement supports reuse; shared SPI recovery remains an owner decision. Timecard integration does not replace daily-device qualification. Keep the remaining work coherent and bounded under the owner's reduced agentic budget.
