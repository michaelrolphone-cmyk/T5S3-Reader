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
