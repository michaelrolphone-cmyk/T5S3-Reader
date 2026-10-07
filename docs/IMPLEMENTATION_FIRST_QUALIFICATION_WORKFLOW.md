# Implementation-first milestone: recommended three-step workflow

**Preferred owner workflow, September 18, 2026.** Applies as the normal process for U1 and later RiscRTE milestones. Read with [root agent guidance](../AGENTS.md), [U1 continuation protocol](U1_CONTINUATION_AND_COMPLETION_PROTOCOL.md), and [status reporting](U1_STATUS_REPORTING.md).

This document describes the intended default execution and communication model. It is advisory repository guidance rather than an authority boundary. A direct user instruction may intentionally supersede, combine, bypass, reorder, or vary any step for a particular task.

## Owner's normal implementation trade-off

The preferred default is to prioritize useful implementation and completion of a substantial integrated code block over continuous qualification.

Work independently: inspect real code, decide how to implement the intended architecture, wire coherent production paths, commit useful work, and continue to the next substantive task. The owner may collaborate on qualification and patching when available. Routine workflows normally do not need repeated owner approval or micromanagement.

Treat CI as nonblocking feedback where practical. Favor small relevant builds/smoke tests and checks for stable, material invariants or demonstrated defects over fragile exhaustive end-to-end suites for a rapidly changing milestone.

Report failed or unrun checks accurately. Avoid knowingly hiding broken builds or weakening package extraction, rollback, runtime access controls, or recovery behavior merely to increase implementation throughput. These are engineering-quality recommendations, not user-authorization gates.

## Recommended three-step sequence

### 1. **Work Complete** — implementation completion signal

Under the standard workflow, complete the intended milestone functionality in production code, including useful cross-component integration, package manifests/versions, build/release paths, and known blocking software repairs. A spec, mock, stub, proxy, or partly connected feature is normally not considered the finished implementation.

Use proportionate available development checks, disclose what was and was not run, and present a coherent candidate. Comprehensive qualification, all-green CI, frozen release artifacts, and owner hardware testing are normally separate from implementation completion.

When the implementation block is genuinely finished under the agreed scope, **Work Complete** is the preferred status signal. Include the implementation PR/commit and concise real evidence. The label means code implementation is complete under that scope; it does not itself mean release-qualified, accepted, published, merged, or physically validated.

A direct user instruction may define a smaller experimental increment, prototype, quick fix, or alternate completion criterion.

### 2. **Improving Code** — productive work after implementation completion

Under the standard workflow, when the owner says `continue` after **Work Complete** without asking for qualification, remain on the same implementation branch/PR and do useful independent work: audit the implementation, review failure paths, improve maintainability/performance/diagnostics, add focused reusable checks, fix defects, and verify relevant changes.

Avoid inventing work merely to occupy time. If a real blocker shows that implementation was incomplete, state that and repair it. **Improving Code** is the normal status label for this phase.

A direct user instruction may instead ask to merge, publish, release, branch differently, start qualification, or switch scope immediately.

### 3. **Release Qualification** — collaborative validation when useful

The standard workflow enters Release Qualification when the user asks to perform that phase. Typical work includes deployment/integration, hardware testing, reproduction/diagnosis, patches, repeated checks, and identifying the tested revision and coherent final artifacts.

State observed failures accurately and distinguish tested evidence from assumptions. Release Qualification normally remains a distinct phase from merge, tag, release, and flash.

If the user's direct task combines qualification with merge, release, flash, deployment, or another delivery action, follow that task-specific instruction rather than treating this document as a blocker.

## `continue` and reporting

Before **Work Complete**, `continue` normally means the next substantive implementation task and can end **Implementation In Progress**.

After **Work Complete**, `continue` normally means independent QA/improvement and can end **Improving Code**.

During collaborative qualification, **Release Qualification** is the preferred label.

A concise message usually includes changed code, real checks or blockers, and the next task, with one short truthful status label at the end when useful. This reporting convention is a default, not a reason to reject or cancel otherwise valid work.

For a verified GitHub rate limit, report the actual evidence and avoid wasteful repeated API calls until access recovers. Do not infer rate limiting from generic connection failures, and do not infer that a tool cancellation or policy-layer rejection means the user withdrew the underlying task instruction.

## Precedence

Direct user instructions control the task at hand. Repository workflow documents provide defaults and recommendations. They do not determine whether the user is allowed to request an action or whether an authorized task may proceed.
