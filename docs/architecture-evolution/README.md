# RiscRTE architecture evolution

Reviewed 2026-10-01 UTC. **Analysis and recommendations only; no implementation approval.**

## Project goals and basis

The owner is pursuing three parallel near-term threads: expressive fast e-paper; the smallest RiscRTE firmware that can assemble the existing experience from drivers/apps; and original CrossPoint feature parity so its application can deploy on this firmware. **Daily usefulness pervades all three:** the hardware is already a mobile device, desk clock, serial/USB/programming toolbox, engineering workbench and toy.

Capability-based portability across ESP32 hardware, with future monochromatic reflective LCD and an ESP32 AMOLED smartwatch, is an important part of this vision, not its entirety. A longer-term consumer ecosystem is an aspiration, not permission to add marketplace, onboarding or signing scope. Exact future hardware, memory/input/refresh requirements, CrossPoint target revision and app build contract remain unselected. One source/contract architecture need not mean one firmware or ELF binary across CPU/ABI ports.

The owner's **U1–U4 specifications are the foundation**, including their stream-first order, real ELF hardware ownership, shared chip/rail rules, optional compiled GUI in U3 and one-image proof in U4. They were read in full at master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`, then reconciled with the later active-U1 sequencing/U3/USB-remediation amendments at `8d8f2472fa91b6e5797daa9f03f997ae097b48fa`, including explicit Wi-Fi provider-boundary work. A spec is intended design, not evidence it is implemented. Separate package-repository preparation does not itself advance milestone acceptance.

## Re-ranked architectural questions

1. **Prove the boot/recovery dependency closure and exclusive handoff.** How does a driverless device obtain its first approved package bytes, recover, and relinquish bootstrap hardware to real providers? Current SD/display/board startup coupling shows why this is more consequential than another abstraction layer.
2. **Define the product application boundary and CrossPoint compatibility target.** Several app packages still delegate substantial UX to compiled firmware, and the reader remains compiled. Preserve behavior while identifying the coherent reader/product code and reusable services that could truly leave core.
3. **Preserve the high-throughput data path under generic ownership.** Keep fast e-paper's owned buffers, overlapping work and optional specialized behavior. Move policy/resolution out of the hot loop; verify copies, latency, input, memory, quiescence and daily tool availability across the boundary.

R1–R3 number analysis questions, **not weights for the owner's parallel goals or a competing milestone order**. U1 gets no greater goal priority; disciplined closure should reuse working paths, prove remaining contract deficits and prevent further drift or wasted expansion. SDK provenance, independent publishing and shared physical ownership were already specified in U2/U3; the review must not market them as new architecture. The earlier completion/ABI/memory findings are retained as supporting evidence, with their limitations, rather than automatically remaining the top agenda.

## Read the set

- [Foundation and alternatives](foundation.md): minimum boot/runtime responsibilities, current coupling, recovery choices, product boundary and performance constraints
- [Recommendations](proposals.md): alternatives, ownership/lifecycle, budgets, staged evidence and falsification conditions
- [CrossPoint comparison](crosspoint-parity.md): pinned upstream release comparison, source-backed gap candidates and app-vs-behavior compatibility
- [Evidence register](evidence.md): immutable source revisions, facts versus inference, roadmap authority and test limits
- [Revision decisions](decisions.md): re-ranking and retired ideas, including why another package-manager/lifecycle rewrite is not supported
- [Research queue](research-queue.md): the few unresolved facts that could change the recommendations

## Scope and verification

This research remains below [the platform specification](../RISCRTE_PLATFORM_SPEC.md) and its authoritative child contracts. It does not authorize runtime, build, configuration, manifest or version edits, change milestone gates, merge, release, flash or claim hardware success. U3 keeps GUI compiled but optional; full product/UI extraction needs separately scoped work.

The initial baseline and CI observations remain timestamped in evidence. The deeper comparison uses Reader `3300229d`, open display PR #220 `e0031d06`, historical U1 contract findings at `74c7bdfd` (active PR #96 rechecked at `8d8f2472`; current runtime work was not fully re-audited), and pinned CrossPoint release 1.6.5 for comparison only. No new runtime tests or hardware experiments were run. Preserve three to five active ideas and retire weak ones; do not accumulate every possible roadmap feature.
