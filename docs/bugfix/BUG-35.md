# BUG-35: preserve legacy language settings until migration commits

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328)
- Canonical key: `michaelrolphone-cmyk/T5S3-Reader::BUG-35`
- Owner: `fix_small_bug_0830`
- Source ref / target branch: `master`
- Baseline: `cff6ef0c11b79634dfa2c688d880df393d8c153c`
- Repair branch: `fix/bug35-preserve-language-migration-source`
- Target PR: draft creation follows this commit; the confirmed PR URL and exact
  remote head are recorded in the linked coordination discussion.
- Claim: [PR #332 comment 5978181715](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978181715)
- Firmware: 1.3.82 → 1.3.109, above the current unmerged 1.3.108 lineage.
- State at this commit: implementation and focused verification complete;
  independent read-only review and focused sanitizer rerun passed; final-head hosted CI pending.

## Failure and correction

`src/CrossPointSettings.cpp::migrateLanguageBinaryFile()` previously renamed
`language.bin`, attempted a save and returned success regardless of whether the
source opened, its two bytes were valid, or the destination save succeeded.
The complete unchanged production implementation reproduces an open failure
that returns true, removes the original pathname and attempts one save.

Read exactly the two-byte version-1 record, validate its version and frozen
language index, and check read and close results before changing settings.
Save the imported value before retiring the source. A failed save restores the
previous in-memory language and leaves the source available for a later boot.
Failed retirement is reported while retaining the successfully saved language
and original file for retry. The old backup is not proactively deleted.
This adds two bytes of stack data and no new heap allocation or polling loop.
Existing boot callers, language mapping, JSON format and settings policy remain.

## Deduplication and ownership

Refreshed all-state PR metadata against the saved complete inventory through
PR #397 and added PR #398. Fresh 348 branches plus 398 PR heads yield 746 references
and 541 unique heads. Targeted inspection of this exact function found the same
faulty ordering in all 540 source-bearing heads; two differ only by comments.
The remaining release-index branch contains only its release index. Current
Grok PR #384 at 3056f514 and X4 PR #350 at 9a209d87 retain the original migration.
Current issue/PR/branch searches and 46 coordination comments had no competing
BUG35 repair. Runtime `RiscRTE` main `be74e940` is headless; its source map has no
CrossPointSettings migration. Runtime PRs #1–4 are merged; active runtime work is
separate. BUG-51's similar Recent Books migration is separately repaired.

Shared ledger `automation/bug-ledger` remains at 13f6c55 with its recorded sole
writer. This repair does not edit it or claim canonical integration. PR #397/398
are now closed unmerged and incorporated by the separate Grok PR #384 owner;
those PRs/branches are preserved. No default-branch write, merge, release,
deployment or device operation is part of this repair.

## Verification

`test/run_language_migration_test.sh` compiles the complete production settings
source/header and Serialization header with the actual generated I18n mapping.
Storage and JSON persistence endpoints are fixtures. All 22 frozen language
indices, source absence, short/oversized/invalid records, open/read/read-error/
close/save/rename failures, existing-backup collision, retries, repeated calls,
ordering, resource cleanup and existing JSON/legacy/language-only boot callers
pass in normal and ASan/UBSan runs. The same regression against the unchanged
baseline fails its first source-preservation assertion. It is included in the
existing Springboard host aggregate.

Changed-package versions, shell syntax and whitespace checks pass. The complete
production settings translation unit also compiles to 32-bit Xtensa with fixture
storage/Arduino endpoints; that is not a full firmware build. Local LeakSanitizer
is disabled under ptrace; AddressSanitizer and UndefinedBehaviorSanitizer remain
active. Physical SD behavior, interrupted-power durability, real JSON persistence
faults, language rendering and hardware qualification were not tested. Exact-head
CI and final readiness are recorded in the PR and terminal coordination comment.
