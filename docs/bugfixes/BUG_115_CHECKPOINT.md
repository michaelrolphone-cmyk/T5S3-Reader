# Canonical bug 115: UTF-8 bookmark summaries

## Active claim, 2026-10-03 16:48 UTC

- Owner: queued 16:30 Reader repair, `fix_reader_queue_item`; canonical ID **115**. Baseline master `9b673f3b10e20a24d11f9b71f0deef7323b0a714`; repair branch `fix/bug115-bookmark-utf8-summary` directly targets master.
- Phase: confirmed, claimed before production edits. Linked production `BookmarkUtil::sanitizeBookmarkSummary` regression fails on the baseline (assertion exit 134): 71 ASCII bytes followed by `é` is truncated at byte 72 into invalid UTF-8. `EpubReaderActivity::addBookmark` uses the result as the persisted bookmark summary. The adjacent `std::unique` comparator passes signed chars to `std::isspace`.
- Scope is Reader-owned firmware utility code, not migrated Apps/Drivers. No EpubReaderActivity source change is needed. Preserve the existing 72-byte bound and whitespace policy; reuse the existing UTF-8 boundary helper and unsigned-byte ctype classification.
- Dedup: all 379 PRs in every state and 707 fetched branch/PR refs inspected. The 706 refs with `src/util/BookmarkUtil.cpp` all contain blob `f18b0f4dc4a3faa98028a843390bcf267119f918`; no existing repair. Bookmark issue/PR searches and authoritative ledger claims show no #115 owner. Open, merged and closed-unmerged repairs remain untouched.
- Reserve firmware **1.3.83**, above master/published 1.3.69 and all observed open reservations through 1.3.82. Fresh reservations checked: #350 1.3.79, #378 1.3.80, #373 1.3.82, #379 1.3.82. The identical #373/#379 reservation is intentional consolidation: #373 contains the same ContentOpfParser repair and cache generation 7. Fresh all-branch/ref inspection confirms 1.3.83 is unreserved. No app/driver/provider changes or versions.
- Shared ledger `automation/bug-ledger` remains at `13f6c55cf482738f583efc58cd922e0afe835108`, with prior `task11_wrap42_20261003` claim still recorded. Do not steal or overwrite its ownership. This per-bug checkpoint is the owner-authorized durable claim; reconciliation into the single shared ledger is deferred to its coordinator.
- Verification next: normal ASCII and multilingual text, all 2/3/4-byte boundary positions, exact/over-limit input, empty/whitespace input, malformed byte safety, repeated calls; applicable target builds and exact-head CI. This pure string utility has no I/O, retry queue, resource acquisition or teardown state. No hardware claim.
- No master write, merge, release, signing, deploy, hardware, device or user-computer action.
- Provenance: [canonical report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13f6c55cf482738f583efc58cd922e0afe835108/bugs.md#115-bookmark-summary-truncation-can-persist-invalid-utf-8-for-ordinary-non-ascii-page-text), source scan `3b7ba1d3ab884ce95f02f835e265d030ef4b7724`. Original report and provenance remain unchanged.

## Implementation checkpoint, 2026-10-03 16:51 UTC

- Minimal production repair is implemented: unsigned-byte whitespace comparison and `utf8SafeTruncateBuffer` at the existing 72-byte bound. No bookmark path/storage behavior or malformed-input repair policy changes.
- PASS locally: 1,199 UTF-8 boundary/byte cases plus whitespace and repeated-call checks, strict C++17 warnings; same focused regression under ASan/UBSan (`detect_leaks=0`, no leak-detection claim); direct Xtensa ESP32-S3 compilation of BookmarkUtil.cpp and Utf8.cpp; changed-package version guard; shell syntax and whitespace checks.
- Baseline regression failure remains independently preserved; full host aggregate and both supported firmware builds are requested through exact-head PR CI. Local targeted cross-compilation is not a full firmware/link/device test.
- Claim remains active until software checks reach a terminal checkpoint in this PR. Shared-ledger reconciliation remains deferred without altering its recorded owner. No physical-device testing is a prerequisite to review readiness.
