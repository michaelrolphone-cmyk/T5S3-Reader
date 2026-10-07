# Preserve the Recent Books migration source

- Repository: `michaelrolphone-cmyk/T5S3-Reader`
- Canonical key: `michaelrolphone-cmyk/T5S3-Reader::BUG-51`
- Owner: `fix_small_bug_0730`
- Source ref / target branch: `master`
- Baseline: `cff6ef0c11b79634dfa2c688d880df393d8c153c`
- Repair branch: `fix/bug51-preserve-recent-migration-source`
- Target PR: pending publication; the claim and later terminal record are in
  [coordination PR332](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977729177).
- Affected source: `src/RecentBooksStore.cpp::loadFromFile`, legacy `recent.bin` migration.
- Firmware: `1.3.82` baseline to reserved `1.3.108`; other active reservations
  through `1.3.107` were checked. No app/driver/provider package changes.

## Reproduction and fix

The production migration method reported success and renamed `recent.bin` even
when `saveToFile()` returned false. A transient JSON-publication failure could
therefore retire the only usable persistent history before the next boot.

Require successful JSON publication before attempting retirement. A failed save
keeps the original binary available for retry. A failed rename reports failure
without claiming that migration completed; a successfully published JSON file
remains usable on the next load. Existing JSON-first behavior and binary formats
are unchanged. The change adds no loop, allocation, I/O retry or API.

## Verification

`test/recent_books/migration_test.py` compiles the complete production store,
its real header and real binary serializer. In-memory storage, JSON publication
and book metadata are fixtures. It covers a two-record v3 migration, failed and
repeated failed saves, fresh-boot retry, publish-before-rename ordering, rename
failure, backup collision, open failure/retry, unknown binary version, omitted
title resave, JSON-first behavior and descriptor cleanup. The original baseline
must fail the first save-failure assertion. Sanitizers are included in the
Springboard aggregate. Use `--source` to compile a preserved baseline source.

Normal and ASan/UBSan focused runs passed locally. Local LeakSanitizer is disabled
for sandbox ptrace; this is not a leak-detection claim. Full aggregate and exact
remote-head CI results will be recorded in the PR and terminal claim comment.
No physical SD, power-loss durability, device, release or hardware qualification
is claimed. JSON publication atomicity is a fixture boundary, not retested here.

## Scope and coordination

Fresh preflight inspected all 397 Reader PRs and 347 branch heads: 540 unique
commits, 539 with RecentBooksStore source. Both source variants have the same
faulty migration method; the release-index branch has no implementation. Targeted
issues/PRs and current claims had no competing repair. RiscRTE's populated `main`
at `be74e9408577b9a58d86bfae4e6050c8a7fb88ab` and its source map have no equivalent
Recent Books store, so the owning repository remains Reader.

The shared ledger at `13f6c55cf482738f583efc58cd922e0afe835108` and its recorded
writer remain untouched; this isolated claim is queued for reconciliation.
Legacy string validation (BUG-39), v2 record alignment (BUG-47), unrelated PRs,
merges, releases and device actions remain outside this repair.
