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

## Implemented and locally verified

- The regression fails on archived current-master production source: `mixed-case external URI became an internal footnote`. It uses the production classification and anchor start/close blocks with the repository's actual Expat C sources. Storage, layout and surrounding parser state are host fixtures; this is not a whole-reader or physical-device test.
- Replace the lowercase denylist with ASCII URI-scheme recognition, following [RFC 3986 section 3.1](https://www.rfc-editor.org/rfc/rfc3986#section-3.1). Mixed-case and previously unlisted absolute schemes, network-path references and blank hrefs do not enter the internal Footnotes path. Relative paths and fragments retain their original targets and existing styling.
- Section cache version 23→24 rejects/closes/removes old chapter caches, so old external-link entries are regenerated. Book metadata cache, NCX logic and PR374 remain untouched. Reopening a cached chapter incurs one regeneration.
- PASS: 96 production anchor cases over Expat chunk sizes 1/7/512/1024, mixed external/internal sequences, blank/missing references, malformed XML followed by a fresh retry; production section-cache header/load/cleanup methods test stale version rejection, failed cleanup/retry, read-open failure, new-cache roundtrip and parameter mismatch. No OPDS or XML cleanup code changes.
- PASS: package source-version guard, native UI contract, seven Home shortcut contracts, shell syntax and whitespace checks. Local ASan/UBSan run timed out at 30 seconds and is not a pass. Linux CI runs this regression with both sanitizers; full local board builds omitted to preserve resources for higher-priority Watch/X4 work.
- Pre-publication refresh: master/ledger/old42 remain at the recorded SHAs. PR350 advanced to `67d0fd3e9f4012eae681e7d284d2d0bb39732dfb`, still firmware 1.3.79 and no affected-path overlap. Own firmware 1.3.80 remains reserved; consolidation owner separately reports upcoming 1.3.81. No version increase beyond 1.3.80 needed here.
- Claim remains active until exact-head CI and remote verification are terminal. This file supplies reconciliation evidence only; shared ledger update remains deferred.
