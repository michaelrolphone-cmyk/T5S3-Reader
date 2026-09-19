# Implementation-first milestone: three-step workflow

**Binding owner-directed workflow, September 18, 2026.** Applies to U1 and later RiscRTE milestones. Read with [root agent instructions](../AGENTS.md), [U1 continuation protocol](U1_CONTINUATION_AND_COMPLETION_PROTOCOL.md) and [status reporting](U1_STATUS_REPORTING.md). This governs execution and communication, not permission to omit specified functionality or conceal known defects. Its latest owner-directed process supersedes older procedural demands for exhaustive CI, qualification, same-commit evidence packages or physical testing before implementation completion.

## Owner's deliberate trade-off

**Prioritize quantity of useful implementation and completion of a substantial integrated code block over continuous testing and qualification.** Work independently: inspect real code, decide how to implement the specs, wire complete production paths, commit coherent work and continue immediately to the next task. The owner will collaborate on qualification and patching when available; do not ask the owner to approve routine workflows, test individual changes or micromanage coding. CI is nonblocking feedback. Use small relevant builds/smoke tests and tests for stable, material safety invariants or demonstrated defects; do not spend weeks developing fragile exhaustive end-to-end suites for a rapidly evolving milestone. Never represent failed/unrun checks as passed, ignore a known broken build, or knowingly compromise safe package extraction, rollback or runtime authorization to produce more lines of code.

## Exactly three steps

### 1. **Work Complete** — implementation completion signal

Finish ALL required milestone functionality in real production code, including cross-component integration, package manifests and versions, build/release paths, and fixes for known blocking software defects. A spec, mock, stub, proxy or partly connected feature is not complete. Use proportionate available development checks, disclose what was and was not run, and present a coherent code candidate. Comprehensive qualification, all-green CI, exact frozen release artifacts and owner hardware testing are **not** prerequisites for declaring this implementation step finished.

Once the implementation block is actually finished, announce **Work Complete** prominently as the standalone bold FINAL STATUS LINE. Include the implementation PR/commit and concise real evidence immediately above it; this says only code work is complete, NOT that a release is qualified, accepted, published or physically validated. The owner may not have time to test at this point; do not repeatedly ask or impose an additional phase.

### 2. **Improving Code** — productive work while awaiting owner direction

When the owner says `continue` after **Work Complete** but has not explicitly advanced the project to Release Qualification, remain on the SAME milestone implementation branch/PR and do substantial independent work: audit implementation, review failure paths, enhance maintainability/performance/diagnostics, add focused reusable checks, fix discovered defects and verify relevant changes. Avoid inventing scope or fabricating work to fill time. If a genuine blocker reveals implementation was incomplete, say so, fix it, and announce **Work Complete** again when corrected; otherwise the final status for every such response is **Improving Code**. Do not enter qualification on your own, await CI idly, merge, publish or request repeated owner testing.

### 3. **Release Qualification** — explicitly owner-initiated collaboration

**Only the owner's explicit request advances to Release Qualification.** Together perform deployment/integration and actual hardware testing, reproduce/diagnose defects, patch the milestone, repeat meaningful checks, determine a tested revision and coherent final artifacts, and decide whether the candidate qualifies for release. State observed failures honestly. Do not assert the release is qualified until tests were actually performed and the owner accepts the result. The step remains **Release Qualification** during testing, debugging and readiness decisions; owner-accepted qualification is the outcome of this step, not a fourth workflow step. Merge/tag/release/flash are separate explicit owner-authorized actions; the next milestone starts after the owner authorizes transition.

## `continue` and reporting

Before step 1, every `continue` does the next substantive implementation task and ends **Implementation In Progress** (a progress label, not a fourth workflow step). The implementation-finished turn ends **Work Complete**. Afterward, `continue` does independent QA/improvements and ends **Improving Code** until the owner explicitly says to begin release qualification. During that joint phase end **Release Qualification**. Each message is a few concrete sentences: changed code, real check or blocker, next task. The LAST line is exactly one standalone Markdown-bold 2–5-word label and has no text after it. On a verified GitHub rate limit report why GitHub-dependent work is blocked and last confirmed SHA/reset if provided, stop repeated API calls, and resume only when access is restored on a later user request; do not invent a reset or infer quota from a generic connection error.
