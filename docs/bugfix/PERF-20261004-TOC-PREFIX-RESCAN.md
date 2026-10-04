# Base integration checkpoint — 2026-10-04 08:00 UTC

- repository_full_name: michaelrolphone-cmyk/T5S3-Reader
- stable source alias: michaelrolphone-cmyk/T5S3-Reader::PERF-20261004-TOC-PREFIX-RESCAN
- canonical_key: pending sole-coordinator assignment
- owner: fix_reader_performance_0610
- source_ref / target_branch: xteink-x4-pro-boot
- original reproduction baseline_sha: 60e5a78b79e5a196e2265aa8bad3200af5a82479
- refreshed integration baseline_sha: 9a209d8794d725393ddbb48d3880c03b9af29ba4
- captured repair head / first parent: b58ca427c0872fbcb9d83ab380e951fa124d1d02
- merge second parent: 9a209d8794d725393ddbb48d3880c03b9af29ba4
- repair_branch: perf/toc-prefix-rescan
- target_pr: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/396
- resumed claim: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977913791
- phase: local merged-tree validation; new exact-head CI pending

The working base advanced to firmware 1.3.105 and made this existing repair unmergeable. Merge it into the existing repair branch without rewriting either history. The only conflict was the platformio.ini version line: retain this repair's unshipped 1.3.106, above the actual 1.3.105 base. Separate PR398's 1.3.108 reservation is not part of this integration and does not justify renumbering the same unshipped repair. No separately distributed package is changed relative to the new base.

Preserve every new PR350 file byte-for-byte except that explicitly resolved version line. The exact-href TOC implementation, focused fixtures and Springboard test hook remain byte-identical to the preceding repaired head. Firmware 1.3.105 app inventory, touch epochs, frontlight 0.1.3, provider diagnostics, boot logo, workflows and their tests are retained. The original lower-read-cost/output-equivalence evidence below remains valid for the unchanged TOC implementation; all tests and hosted checks must be re-evaluated for this combined tree.

No PR350/default/ledger branch write, merge of the PR, release, deployment or device action. The earlier completion text below is historical; final status is recorded in PR396 and the original ledger conversation after exact-head CI completes.

---

# TOC prefix-rescan repair checkpoint

- repository_full_name: michaelrolphone-cmyk/T5S3-Reader
- stable source alias: michaelrolphone-cmyk/T5S3-Reader::PERF-20261004-TOC-PREFIX-RESCAN
- canonical_key: pending sole-ledger-coordinator assignment
- owner: fix_reader_performance_0610
- source_ref / target_branch: xteink-x4-pro-boot
- baseline_sha: 60e5a78b79e5a196e2265aa8bad3200af5a82479
- source_pr: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350
- repair_branch: perf/toc-prefix-rescan
- target_pr: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/396
- file_paths: lib/Epub/Epub/BookMetadataCache.cpp/.h; test/epub_toc_index/; test/run_springboard_test.sh; platformio.ini
- report: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977145950
- durable claim: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5977265265
- firmware reservation: 1.3.106 (1.3.105 reserved independently by the PR350 owner)
- phase: locally verified implementation; exact-head hosted CI pending

PR350 and PR348 remain open; master is cff6ef0c11b79634dfa2c688d880df393d8c153c. The approved temporary working-branch base applies. Neither owner branch is modified by this repair. Current source deduplication covered 742 branch/PR-head refs with nine distinct BookMetadataCache blobs; all retained the small-spine prefix loop. Fresh claim/report and all-state TOC/updated-PR searches found no competing repair. Existing BUG173 TOC fallback and BUG261 path resolution remain distinct.

The shared ledger stays at 13f6c55 with its recorded writer. This per-repair checkpoint queues consolidation without changing canonical IDs or taking over that writer. No merge, release, deployment, firmware validation, device operation or hardware claim.

## Implementation and measured scope

For fewer than 128 spine rows, read the immutable temporary spine once into an exact-href table. Bound it to 127 string objects and 8192 aggregate href bytes, with a 1000 ms optional-build budget. Check complete component reads; a partial/error/oversized/budget-limited optimization is discarded and the existing exact on-disk scan remains available. The scheduler yields every 16 rows or 20 ms, including tick rollover. End-of-pass clears/releases the table; new write/pass admission clears its validity. Large-book hash indexing is unchanged.

Exact matching preserves first duplicate, missing targets, empty paths, UTF-8/embedded bytes and TOC order/title/href/anchor/level. No cache generation or serialization change; valid warm caches do not rebuild. No layout, visuals, navigation, defaults, error UI, feature or package payload changes. Firmware 1.3.102 → 1.3.106 only; no app/driver/service/provider version change.

The real production BookMetadataCache, Serialization and TocNavParser with real Expat are compiled in the durable regression. Storage/time/logging and already-canonical path normalization are host fixtures. The same 1270-link document, with reverse/interleaved chapter links and both 4096-byte and one-byte XML chunks, changes from 325120 spine read calls to 508 at 127 spines. An added unused 128th spine still uses 512 reads. The final small-spine path performs zero per-link storage reads. This is a 640× reduction in measured host storage API calls, not a device speed/FPS or physical-sector result.

The original and repaired 1270-entry serialized TOC files compare byte-for-byte equal, SHA256 ba78e810ab875a3f1abdf8dd58c059f1ba1a464a280954e0d80ec51eacc57343. Every title, href, anchor, level and spine mapping is independently checked.

## Verification

Passed locally:
- Strict C++17 normal and AddressSanitizer/UndefinedBehaviorSanitizer focused regression; original production code fails the new storage-read bound. Baseline cost control passes and reproduces 325120 reads.
- 1/32/64/100/127/128/200 spines; equal-output 127/128 boundary; reversed/interleaved repeats; exact first duplicate/case/empty/missing targets; inclusive 8192-byte cap and single/aggregate overflow fallback.
- Negative and short reads at each of eight record-component positions; initial rewind failure on complete and partial optimization; read/write admission failures; repeated large-index admission; closed-handle refusal; retry, same-object reuse and temporary-file cleanup.
- Item/time scheduler yields, 1000 ms fallback and 32-bit timer rollover. The optional deadline does not cancel or change the legacy fallback result.
- Full local Springboard and NativeApp aggregates; changed-package version guard, Python/shell syntax and whitespace. Final additional fault cases also pass with sanitizers.
- Production BookMetadataCache compiles for 32-bit Xtensa ESP32-S3 with no exceptions/RTTI against host boundary declarations.

Not yet established: exact-head hosted checks and complete firmware targets. No local full firmware build, physical EPUB/SD timing, target heap peak or hardware qualification. Local LeakSanitizer is unavailable under the executor ptrace restriction; local ASAN_OPTIONS=detect_leaks=0 only, with ASan/UBSan and hosted defaults retained. The additional lookup storage is bounded by the payload cap plus 127 string objects/container overhead; actual allocator overhead is target-dependent. Inputs exceeding the cap retain old scan cost; the optional first pass can add bounded work before fallback.

Next: publish non-force from the captured claim parent 87ec175f1c26e2f86a90d3b17b9151a5e6db4332, verify remote tree/SHA, review exact-head CI and record its terminal outcome in PR396 and the original ledger conversation. Do not call this fixed on master until actual integration is verified.
