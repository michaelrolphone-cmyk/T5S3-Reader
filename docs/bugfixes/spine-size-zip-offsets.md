# Small-spine EPUB chapter-size lookup

## Identity and scope

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
- Alias: `PERF-20261004-SPINE-SIZE-ZIP-WRAP`; canonical number awaits sole-coordinator reconciliation.
- Source/integration base: `xteink-x4-pro-boot`, `9a209d8794d725393ddbb48d3880c03b9af29ba4`.
- Repair branch: `perf/spine-size-zip-offsets`; firmware **1.3.105 → 1.3.114**, above reservations through 1.3.113. No independently distributed app/provider changes.
- [Report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979205098) and [isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979352200).
- PR350/348 are unmerged. The selected feature base is not represented as integrated on master. No owner/default/coordination branch, merge, release or device action.

## Defect and repair

Below 128 spine items, `BookMetadataCache::buildBookBin` uses one already-open ZIP but performs circular central-directory scans for each chapter size. A directory arranged opposite to reading order repeats almost the entire directory per chapter.

For 8–127 spine items, the builder now opts into optional offset hints without doing I/O. The ordinary scanner first proves a wrap occurred. Only the next size lookup attempts preparation, once per open handle. Sequential-prefix books keep their existing cheap path; one-to-seven-item and >=128-item behavior is unchanged.

The optional table is capped at **1,024 entries / 16 KiB** and uses one nonthrowing allocation. Preparation has a 15-second budget, entry/16-KiB/20-ms scheduler checkpoints, exact reads and checked seeks/ranges, and a complete expected-directory/EOCD terminator check. Ineligible counts, allocation failure, incomplete records, bounds or deadline leave ordinary lookup available. These are optimization eligibility checks, not a changed archive acceptance policy. Existing EOCD parsing and the ordinary scanner remain unchanged.

Hints contain only hash and offsets. A unique hash bucket rereads the actual central record, exact C-string name, next offset and current size. Duplicate names and hash collisions use the original circular scanner, preserving its cursor-sensitive member selection. Hinted successes advance the same cursor. Hints are discarded on failed validation, close or reopen; preparation is not retried automatically. There is no persistent cache, all-entry name map or cached size field.

PR396 changes TOC-to-spine lookup in the same BookMetadataCache source, not this builder. PR400 changes CSS ZIP handling with first-match semantics. This repair is independently based on the selected baseline and does not import either repair. Integrators must preserve their separate members/methods and changes when resolving file/version conflicts.

## Measured behavior

The original report's complete-production probe, with 127 chapters and 642 ZIP members, now measures:

| Archive order | Original reads / seeks / bytes | Repaired reads / seeks / bytes |
|---|---:|---:|
| Reverse chapters | 732,799 / 325,886 / 3,322,491 | 13,084 / 6,029 / 101,433 |
| Forward chapters | 5,779 / 2,696 / 27,213 | unchanged |

The reverse case uses 10,272 hint bytes. The 4,405-byte host-layout `book.bin` remains identical, SHA256 `e1e308eec55838195ad249fae543442e7600f89a9af6c4161272b7323f317b2c`.

Independent review found an initial eager-preparation regression; the deferred design above fixes it. With eight chapters first and 1,016 unused resources after them, both original and final code perform **73 reads, 41 seeks, 1,344 bytes, no yields and no hint allocation**. The test now retains this case. An isolated wrap can still incur bounded preparation overhead; this is not a universal speedup claim.

These are logical fixture storage calls and bytes, not physical SD sectors, device latency, FPS or a measured owner's-library workload.

## Regression and verification

Run `python3 test/epub_spine_sizes/size_lookup_test.py`; the Springboard aggregate invokes it. It compiles the complete production BookMetadataCache, ZipFile, InflateReader and inflater with real Serialization and the verbatim production path normalizer. Storage, clock, logging and scheduler are fixtures.

- 82 normal/ASan builder runs cover 1/7/8/16/64/127/128 spines, forward/reverse/front layout, stored/deflated ZIPs, exact 1,024/1,025-entry boundaries, normalization, allocation/read/seek/open fallback, time bounds/rollover, retry and balanced cleanup.
- 72 healthy original/repaired serialized snapshots match exactly. An original-source run with the optimized-cost assertions fails at the reverse-order workload.
- Direct production ZIP checks cover rotating duplicate members, zero size, embedded-NUL C-string behavior, oversized names, missing members, injected equal-hash buckets and false candidates, current size rereads, negative/zero/short reads, seek failures, malformed-prefix fallback, and close/reopen lifetime.
- Independent review and full focused rerun pass after the prefix-order correction. Target Xtensa 32-bit translation-unit compilation covers both changed production units with declared HAL endpoints.
- Full final local aggregates and exact-head hosted firmware/host checks are recorded in the PR/checkpoint as they complete; this committed record does not predeclare their result.

Local runs set `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer is unavailable under this executor's ptrace setup. The test inherits its caller environment; hosted defaults are unchanged. UBSan is not claimed for this focused ZIP test because separately reported BUG256 concerns inherited unaligned EOCD loads. General EOCD validation/read handling, serializer/publication failures, malicious concurrent same-handle archive replacement and full parser safety remain separate issues. No full local firmware link, physical SD/device or visual qualification is claimed.
