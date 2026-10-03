# RiscRTE architecture evolution

Reviewed 2026-10-02 UTC. **Analysis and recommendations only; no implementation approval.**

## Project goals and basis

The owner has **four equal parallel goals**: expressive fast e-paper/apps; the smallest foundational core composing the existing UX from packages; CrossPoint feature parity through apps/drivers; and reliable daily mobile-device/workbench use, including the desk clock, serial/USB/programming tools and games.

Capability-based portability across ESP32 hardware, with future monochromatic reflective LCD and an ESP32 AMOLED smartwatch, is an important part of this vision, not its entirety. A longer-term consumer ecosystem is an aspiration, not permission to add marketplace, onboarding or signing scope. Exact future hardware, memory/input/refresh requirements, CrossPoint target revision and app build contract remain unselected. One source/contract architecture need not mean one firmware or ELF binary across CPU/ABI ports.

The owner's **U1–U4 specifications are the foundation**, including their stream-first order, real ELF hardware ownership, shared chip/rail rules, optional compiled GUI in U3 and one-image proof in U4. They were read in full at master `3300229d0a232b4e6047a7c93b2f518c033c3cfa`, then reconciled with the later active-U1 sequencing/U3/USB-remediation amendments at `8d8f2472fa91b6e5797daa9f03f997ae097b48fa`, including explicit Wi-Fi provider-boundary work. This pass reread the full platform, hardware boundary, sequence and U1–U4 specifications at `5466d93ca6916342d304af10d681208db2566de6` and confirmed all seven unchanged at `64d19d645720cba5c207f67c5d7fa1a031a07c0a`. All seven were reread at merged master `82caa0997e913f01c1f5f9ab942d056bc9f04a82` for this review. A spec is intended design, not evidence it is implemented. Separate package-repository preparation does not itself advance milestone acceptance.

## Ranked recommendations for the next evidence

The owner's developer-usable **1.0 beta** target is end of next week (planning date **2026-10-09**, not an independently confirmed release date or a firmware-version downgrade). Rank the next useful evidence, not the four goals or milestone order:

1. **R1 — Establish the actual board's complete bootstrap and recovery baseline.** Reuse merged verified-byte loading, SD-installed packages and CAM app execution. Default-app selection in paper firmware occurs after board/SD/display startup; it is not a minimal boot path. Preserve compatible firmware **and installed package generations**, informed by PR347's restore failure. X4 now has physical pattern/text evidence and a shared-renderer provider path; Home still needs a mounted provider-owned filesystem and Reader binding. Retain that boundary without replacing matching public ABIs [E22](evidence.md#e22-integrated-bootstrap-is-a-firmware-and-package-contract-not-an-abi-rewrite).
2. **R3 — Restore measured daily and expressive performance.** After baseline and actual X4 work, prioritize the owner's approximately one-minute splash / three-minute clock-update regression. Cause is unverified. Separate cold boot, timer wake, provider/SD work, rendering, accepted flip and physical settling before choosing a correction; retain no-copy paths, overlap and visual quality.
3. **R4 — Retain the clean external developer round trip as the next reuse witness.** Build → canonical package → ordinary install → launch → update → recover remains useful, but does not displace the owner's baseline/X4/performance order or justify reimplementing merged U1 machinery.
4. **R2 — Defer wholesale product extraction until the baseline works.** Preserve CrossPoint behavior, book/state/transfer paths and optional compiled GUI. Hardware-specific firmware cleanup and contingent driver/PaperSpace extraction follow the owner's direction; they are not a diagnosed cure for the T5S3 regression.

The four goals remain equal; stable IDs denote questions, not milestone acceptance. U1 #96 and CAM #344 are merged. [SD/shared-SPI manual-reboot retention](recovery-contract.md) is now approved and implemented, superseding the earlier undecided-policy recommendation. PR347 is now merged at `b22a4669`; the earlier pending-approval note is historical. [E23](evidence.md#e23-x4-shared-renderer-reuse-and-the-unmounted-volume-boundary) separates physical X4 display proof from later untested integration. No closed PR edit, new implementation writer or device operation belongs to this review.

## Read the set

- [Foundation and alternatives](foundation.md): minimum boot/runtime responsibilities, current coupling, recovery choices, product boundary and performance constraints
- [Shared-bus recovery history](recovery-contract.md): approved retained-fault disposition and earlier alternatives
- [Recommendations](proposals.md): alternatives, ownership/lifecycle, budgets, staged evidence and falsification conditions
- [CrossPoint comparison](crosspoint-parity.md): pinned upstream release comparison, source-backed gap candidates and app-vs-behavior compatibility
- [Evidence register](evidence.md): immutable source revisions, facts versus inference, roadmap authority and test limits
- [Revision decisions](decisions.md): re-ranking and retired ideas, including why another package-manager/lifecycle rewrite is not supported
- [Research queue](research-queue.md): the few unresolved facts that could change the recommendations

## Scope and verification

This research remains below [the platform specification](../RISCRTE_PLATFORM_SPEC.md) and its authoritative child contracts. It does not authorize runtime, build, configuration, manifest or version edits, change milestone gates, merge, release, flash or claim hardware success. U3 keeps GUI compiled but optional; full product/UI extraction needs separately scoped work.

Historical findings remain timestamped in [evidence](evidence.md). Current delta uses master `3722a3f4` and open X4 `94c14bf6`; earlier checkpoints remain historical. The governing U1–U4 specs and root instructions are unchanged from the full `82caa099` read. No runtime test, serial/device operation, firmware release or milestone acceptance occurred in this documentation pass. Keep three to five active ideas; retire weaker assumptions instead of accumulating scope.
