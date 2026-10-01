# RiscRTE architecture evolution

Last reviewed: 2026-10-01 UTC. Status: **recommendations for discussion, not approved implementation**.

RiscRTE already has useful generic provider, lifetime and package foundations. The highest-leverage next work is making the contracts between independently evolving modules precise and verifiable, rather than replacing those foundations or adding more framework subsystems.

## Stated context and unknowns

The owner's clarification on 2026-10-01 describes one important part of the platform vision: foundational ESP32 firmware with downloadable drivers exposing capabilities, so applications depend on the capabilities they need rather than board identity. Current emphasis is display-driven configurations, especially e-paper, with monochromatic reflective LCD and an ESP32 AMOLED smartwatch planned next. This does not make the entire platform display-specific.

Three near-term threads remain parallel:
- Push e-paper as an expressive medium, including fast rendering
- Minimize RiscRTE core code while assembling the existing user experience from drivers and apps; moving ownership must preserve valuable functions
- Reach feature parity with the original CrossPoint product through apps and drivers so its app can deploy on this firmware

Evaluate recommendations against all three. A portable baseline must leave room for optional high-performance e-paper paths; a smaller core is not success if current UX disappears; CrossPoint compatibility is broader than today's T5S3 board. The target CrossPoint revision, app integration/build assumptions and feature-parity checklist remain unspecified, so parity is not claimed.

This is a portability goal, not proof that every ESP32 variant can use one identical firmware or application binary. CPU/ABI ports and bootstrap requirements may differ, as the platform specification already allows. The specific future chips, panels, geometry, color modes, refresh behavior, inputs, RAM/PSRAM and power budgets are not yet established here; no dates or hardware support claims follow from the clarification.

## Ranked recommendations

1. **Make display behavior portable as well as its lifetime guarantees truthful.** Use negotiated geometry, formats and supported presentation behavior rather than e-paper or board assumptions; keep buffer reuse, presentation completion and physical quiescence distinct. Preserve the useful PR #220 surface work, then remove dependence on firmware-owned physical display behavior without losing its safeguards. High confidence in the current contract gap; future LCD/AMOLED behavior remains to be specified and verified.
2. **Define one compatibility contract for capabilities and independently released SDK consumers.** Keep the existing resolver and lifetime graph. Specify what a higher capability API promises, how incompatible interfaces are represented, and which immutable SDK/runtime versions each independent package supports. High confidence in the release-boundary need; a resolver mismatch bug is not established.
3. **Budget peak resident memory across a whole capability acquisition.** Existing app allocation reclamation is valuable but does not account for provider images, relocation copies and shared display resources as one budget. Start with measured ownership and admission limits; defer a new storage-VM implementation. Medium confidence in priority until peak measurements exist.

The detailed alternatives, ownership, failure behavior, compatibility and measurable outcomes are in [proposals](proposals.md). [Evidence](evidence.md) separates inspected source facts from hypotheses and records exact revisions. [Decisions](decisions.md) records why ideas were narrowed or rejected. [Research queue](research-queue.md) contains the few next questions that could change this ranking.

## Scope and authority

This document set is a research layer under [the platform specification](../RISCRTE_PLATFORM_SPEC.md), [hardware boundary](../HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) and applicable child contracts. It does not supersede them, assign implementation milestones, add qualification gates, or authorize code, configuration, build, manifest, version, merge, release or device changes.

Baseline: Reader master `2b45ab662c0ffe3650ce0f841083ea47886022c9`. PR #220 and U1 PR #96 are analyzed separately as unmerged work; their existence is not evidence that master implements them. Migration repositories are checked at the revisions listed in evidence.

## How this set stays focused

Keep three to five active proposals. A revision must add relevant evidence, materially improve a contract, or change a recommendation; unchanged observations do not justify filler commits. Seek counterexamples before strengthening a claim. Replace or retire weaker ideas rather than accumulating a roadmap. Keep exact code/CI observations separate from proposed acceptance criteria and from physical qualification.

No implementation tests or hardware experiments were run for this documentation change. Existing source tests and exact-head CI records were inspected; their scope and limits are recorded rather than converted into new test claims.
