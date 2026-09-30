# RiscRTE architecture evolution

Last reviewed: 2026-09-30 UTC. Status: **recommendations for discussion, not approved implementation**.

RiscRTE already has useful generic provider, lifetime and package foundations. The highest-leverage next work is making the contracts between independently evolving modules precise and verifiable, rather than replacing those foundations or adding more framework subsystems.

## Ranked recommendations

1. **Make display completion and provider shutdown truthful end to end.** A stable display capability needs separate buffer-reuse, presentation-completion and physical-quiescence guarantees. Preserve the useful PR #220 surface work, then remove dependence on firmware-owned physical display behavior without losing its safeguards. High confidence in the contract gap; no current use-after-free or hardware failure is claimed.
2. **Define one compatibility contract for capabilities and independently released SDK consumers.** Keep the existing resolver and lifetime graph. Specify what a higher capability API promises, how incompatible interfaces are represented, and which immutable SDK/runtime versions each independent package supports. High confidence in the release-boundary need; a resolver mismatch bug is not established.
3. **Budget peak resident memory across a whole capability acquisition.** Existing app allocation reclamation is valuable but does not account for provider images, relocation copies and shared display resources as one budget. Start with measured ownership and admission limits; defer a new storage-VM implementation. Medium confidence in priority until peak measurements exist.

The detailed alternatives, ownership, failure behavior, compatibility and measurable outcomes are in [proposals](proposals.md). [Evidence](evidence.md) separates inspected source facts from hypotheses and records exact revisions. [Decisions](decisions.md) records why ideas were narrowed or rejected. [Research queue](research-queue.md) contains the few next questions that could change this ranking.

## Scope and authority

This document set is a research layer under [the platform specification](../RISCRTE_PLATFORM_SPEC.md), [hardware boundary](../HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) and applicable child contracts. It does not supersede them, assign implementation milestones, add qualification gates, or authorize code, configuration, build, manifest, version, merge, release or device changes.

Baseline: Reader master `2b45ab662c0ffe3650ce0f841083ea47886022c9`. PR #220 and U1 PR #96 are analyzed separately as unmerged work; their existence is not evidence that master implements them. Migration repositories are checked at the revisions listed in evidence.

## How this set stays focused

Keep three to five active proposals. A revision must add relevant evidence, materially improve a contract, or change a recommendation; unchanged observations do not justify filler commits. Seek counterexamples before strengthening a claim. Replace or retire weaker ideas rather than accumulating a roadmap. Keep exact code/CI observations separate from proposed acceptance criteria and from physical qualification.

No implementation tests or hardware experiments were run for this documentation change. Existing source tests and exact-head CI records were inspected; their scope and limits are recorded rather than converted into new test claims.
