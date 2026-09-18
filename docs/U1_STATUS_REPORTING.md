# U1 glanceable progress reporting

**Status:** Mandatory user-facing communication rule. Read with [U1 Continuation Protocol](U1_CONTINUATION_AND_COMPLETION_PROTOCOL.md) and [AGENTS.md](../AGENTS.md). The owner skims briefly; concise, truthful code progress is more useful than repeating the milestone plan or discussing future hardware testing.

Give a few compact sentences: **what code changed** (PR/commit only when confirmed), **what focused validation actually found** (only when relevant), and **the next concrete implementation task or genuine blocker**. Keep long logs, detailed audit inventory and incidental CI status in the compact implementation ledger, not in each response. Do not pretend missing or failed checks passed. CI runs asynchronously and is NOT a reason to stop useful independent work or to build comprehensive milestone-specific end-to-end test infrastructure. Do not repeat the physical-test boundary until software handoff unless the user asks or a physical test is genuinely needed to explain a present blocker.

**Last line of EVERY U1 work or workflow response:** exactly ONE standalone Markdown-bold status phrase of **2–5 words**, absolutely no text after it. Use the current milestone state, not just the current commit. Accurate examples:

- **Implementation In Progress** — mandatory code remains, regardless of unrelated CI status.
- **Verification In Progress** — code is assembled and relevant focused/build checks or review are underway.
- **Build Failure Blocking Completion** — a necessary release-equivalent build is demonstrably broken; fix it while continuing independent available work, never generalize an unrelated CI failure into a total development stop.
- **GitHub Quota Blocking Work** — only for evidenced rate limiting, with concise actual reset/last-SHA facts above.
- **GitHub Access Unavailable** — access failure without confirmed quota evidence.
- **Software Ready For Testing** — required software exists, integrated, no known blocking defect; actual builds/checks and any unverified areas disclosed honestly at handoff.
- **Post Completion Audit Active** — real focused QA after reported readiness, with no discovered blocker.

On the first genuinely completed software handoff include the standalone bold announcement **Work Complete** earlier in the response, identify the implementation commit and the meaningful evidence, then end with **Software Ready For Testing**. Never use that completion phrase for docs, a partially implemented path, or pending implementation. On later `continue`, perform useful audit or fixes rather than repeating status. If a regression invalidates readiness, withdraw the completed assessment.

For a confirmed rate limit say GitHub-dependent work is paused until the limit lifts, without inventing reset timing or retrying repeatedly. The last status line still follows this rule. Never invent commits, CI passes, artifacts, test results or background execution.