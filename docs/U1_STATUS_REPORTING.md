# U1 glanceable progress reporting

**Status:** Mandatory agent communication rule for U1 and every `continue` response. Read with [U1 Continuation and Completion Protocol](U1_CONTINUATION_AND_COMPLETION_PROTOCOL.md), [the platform specification](RISCRTE_PLATFORM_SPEC.md), and [repository agent instructions](../AGENTS.md). This document sets output format only; it does not change milestone acceptance or authorize additional implementation work.

## Every user-facing work report

The owner may glance at the screen for only a moment. Keep the normal response concise and put the immediately actionable facts first: **what changed** (include PR/commit when confirmed), **what validation actually passed or failed**, and **what remains or what is blocked**. Prefer a few compact sentences over long activity logs, repeated specifications, tables, or a history of the whole milestone. Link to the persistent implementation ledger for detailed evidence; put full test logs, exact errors and completed/remaining gate inventory there. Never omit a serious blocker, failed test, missing artifact, unverified CI result or pending physical test merely to shorten the reply. Avoid narrating tool calls and idle progress.

**The last line of EVERY agent message responding about U1 work or its workflow MUST be one standalone Markdown bold status label of TWO TO FIVE WORDS.** Make it the last visible text in the message, with nothing after it (no link, citation, footnote, sign-off or second heading). Use a phrase that accurately summarizes the present work state, not just the operation performed in the current turn; do not claim completion based on docs, partial code, a pending build or a completed subtask. Use exactly one such final label and retain the convention after a chat restart, after a failed test, on a GitHub quota blocker, at initial software completion and during post-completion QA. If requirements or a failure remain, the final label MUST not say complete.

Recommended exact final labels, selected to match verified facts:

- **Implementation In Progress** — U1 mandatory code/test tasks remain; internal checkpoint committed or not.
- **Verification In Progress** — implementation appears in place, but required tests/builds/CI/static checks have not all passed.
- **Build Failure Blocking Completion** — a required build failed; describe the specific failure immediately above.
- **GitHub Quota Blocking Work** — quota/rate limiting confirmed with response evidence; include actual retry/reset only if provided, last confirmed SHA, and explicit pause statement immediately above. Unknown connectivity failures instead use **GitHub Access Unavailable** and disclose that cause is unconfirmed.
- **Software Ready For Testing** — ALL software gates and builds are verified at one SHA; owner physical testing remains pending. This must never mean hardware passed.
- **Post Completion Audit Active** — previously completed software is undergoing useful follow-up QA with no newly discovered blocking regression; if one is found, revert to an incomplete/blocked label.

For the FIRST verified software-ready handoff, the existing protocol additionally requires an earlier, standalone exact bold line **Work Complete**, the verified SHA and the separate statement `Physical hardware acceptance: PENDING OWNER TEST`. That completion announcement is NOT the last line; the last line MUST still be **Software Ready For Testing**. The reserved **Work Complete** phrase must never be used for intermediate progress or merely to satisfy this output-format requirement. On later `continue`, reconfirm it only if completion remains verified and perform real QA before reporting.

This output rule applies equally when no meaningful work could be performed: state precisely why and end with the truthful final label. Do not invent a commit, test result, quota reset, time estimate or background job.