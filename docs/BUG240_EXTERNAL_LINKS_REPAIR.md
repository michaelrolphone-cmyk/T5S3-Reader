# Canonical bug 240 repair checkpoint

- Owner: delegated 14:30 bounded fixer, local task-3; source coordination thread `01a0ff32-b1fe-7710-a75d-2b4380309033`.
- Claim active before source changes; branch `fix/bug240-external-epub-links` from current master `3d9bc4f373679f5ae8dd184db6a8d0afa5a40231`.
- Canonical inventory read from `automation/bug-ledger` at `13f6c55cf482738f583efc58cd922e0afe835108`. This is a bug-specific ownership/evidence checkpoint, not an authoritative tracker. Shared ledger and old bug42 branch remain untouched; reconciliation is deferred to their existing coordinator under the explicit user instruction.
- Existing report #240 identifies the production `isInternalEpubLink` case-sensitive scheme denylist. Source revalidation confirms mixed-case HTTP and `urn:` are accepted as internal footnotes and copied into the reader's navigation list. Production regression is next.
- Checked all 45 open/closed-unmerged PR path lists, all fetched parser branch history and exact-symbol all-state PR search: no competing parser/section-cache repair. Candidate #247 was not claimed or changed because cache invalidation would overlap excluded #374.
- Exclude bug64, #241/#377, #373–#376, active Watch/shared app work and all other repositories. No master write, merge, release or device action.
- Firmware reservation: 1.3.80, above all fetched branch versions (maximum PR350 1.3.79; PR377 1.3.78), master/release-index 1.3.69. Check again before publication. No separately released app/driver/provider changes.
- Scope: recognize URI schemes structurally, retain actual relative/fragment references, invalidate only chapter-section caches containing old footnote metadata; production regression and relevant error/retry/cleanup controls. No parser cleanup change.
- Phase: claimed, preparing baseline regression; 2026-10-03T14:38:55.598088+00:00
