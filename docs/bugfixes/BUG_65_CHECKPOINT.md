# Canonical bug 65: EPUB current-directory components

## Active claim, 2026-10-03 17:38 UTC

- Owner: repair_reader_bug_1730_20261003. Baseline master: cff6ef0c11b79634dfa2c688d880df393d8c153c.
- Branch: `fix/bug65-epub-dot-paths`, one focused repair targeting master. Claim published before production/test edits.
- Canonical report: #65 in `automation/bug-ledger:bugs.md`, original September 27 10:22 MDT scan. The shared ledger remains at 13f6c55cf482738f583efc58cd922e0afe835108 with an active task11_wrap42_20261003 owner; reconciliation is deferred. This per-bug checkpoint does not take over that writer.
- Fresh production-linked baseline: `normalisePath("OPS/./chapter.xhtml")` returns `OPS/./chapter.xhtml` instead of `OPS/chapter.xhtml`; test exits 1 on current master. The OPF manifest parser and all three EPUB item-read methods call this normalizer. This is retained Reader code, with no migrated app/driver edit.
- Deduplication: all 380 existing PR states, 710 fetched remote refs, source history and issue searches inspected. No existing normalisePath correction or canonical #65 repair. Other FsHelpers edits concern sorting/classification. Open #379's guide harness is excluded; this repair gets its own tests. PR #380's #115 bookmark repair is complete and excluded.
- Firmware reservation: 1.3.84, beyond master/latest published firmware 1.3.82 and open #380 reservation 1.3.83. Refresh before publication. No app/driver/provider package changes.
- Plan: minimal current-directory omission, production-linked unit and OPF-stream regressions, relevant failure/retry/cleanup cases, target compilation, applicable aggregate checks, draft PR and exact-head hosted CI. Preserve existing parent-path policy and path-root conventions; no URL/percent-decoding redesign.
- Baseline compiler needed `-Wno-unused-variable` for two pre-existing unused sort locals; no unrelated sort cleanup. No hardware/device verification.
- No master write, merge, release, deployment, flash, signing, or unrelated repository action.
