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

## Revision discipline

For each later change record date, affected proposal, new immutable evidence, what was contradicted, and whether the ranking or recommendation changed. Keep superseded ideas here with short rationale, not as active TODOs. Remove questions once answered. A new active idea must displace a weaker one or explain why it belongs in the three-to-five-item set.
