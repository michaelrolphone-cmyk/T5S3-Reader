# Cold CSS ZIP metadata reuse

- Repository: michaelrolphone-cmyk/T5S3-Reader, ID 1367546328.
- Stable alias: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261004-CSS-ZIP-PREFIX-RESCAN`; canonical number awaits the sole ledger coordinator.
- Owner: `fix_reader_performance_0910`; branch `perf/css-zip-prefix-rescan`.
- Source and target: `xteink-x4-pro-boot`, exact baseline `9a209d8794d725393ddbb48d3880c03b9af29ba4` (PR350, open/unmerged). Its feature parent is PR348 at `108daf2050065076346ae016b3b030b1e7ec3e2a`. Master `cff6ef0c11b79634dfa2c688d880df393d8c153c` does not contain this feature baseline.
- [Report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978228422) and [isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978437765).
- Firmware: 1.3.105 → 1.3.110, above the separate PR399 reservation 1.3.109. No app, driver, provider, cache-format or SDK change.

## Cause and repair

Each uncached stylesheet previously constructed one temporary ZipFile to find its size, then another to extract it. Both restarted central-directory lookup. CSS manifest order and ZIP member order need not match; simply keeping a cursor cannot preserve first-duplicate semantics or eliminate arbitrary-order rescans.

A single operation-scoped archive handle optionally collects only the requested, exactly named styles. It retains the first matching member and reuses that same metadata for size admission and extraction. Bounds are 64 requested paths, 4096 input-name bytes, 4096 central-directory entries, and 15 seconds for the optional metadata pass. Storage is two non-throwing allocations (64 compact records plus at most 4096 name bytes). One existing path normalization runs at a time. Item/byte/time checkpoints yield to the scheduler. Header/name reads, bounds and seeks are checked. Nothing is published until all requested entries have been found.

Low heap, allocation failure, missing names, excessive input, I/O error or deadline discard the entire optional cache and retain the existing wrappers. The 64 KiB CSS heap and 128 KiB file-size limits remain unchanged. If memory becomes tight between styles, optional storage and its handle are released before reevaluating the existing heap guard. No optional metadata survives close or reopen. Styles are still parsed in manifest order through the unchanged CSS parser, extraction validation and temp-file cleanup. No archive-wide map or persistent source cache is added.

## Verification

The focused test links the complete production ZipFile, InflateReader, uzlib and CssParser; it extracts the unchanged production normalizer and complete Epub CSS caller/wrappers. Storage and ESP heap/time are fixtures. Original source fails the read-count bound. Normal and AddressSanitizer runs verify exact stylesheet bytes/order, real parser cache serialization/reload, first duplicate ZIP members, reversed/repeated/missing targets, normalized paths, optional allocation faults, existing heap boundary and release-before-fallback, timeout/yields/tick rollover, metadata/seek/open failures, stream/header/temp failures, retry, descriptor/temp cleanup and replacement-archive invalidation.

Deterministic storage-API counts, successful cold loads:

| Chapters / styles | Previous reads | Repaired reads | Previous entry visits | Repaired entry visits |
| --- | ---: | ---: | ---: | ---: |
| 512 / 16, styles late | 150832 | 1095 | 16752 | 531 |
| 512 / 16, styles early | 3376 | 71 | 368 | 19 |
| 1024 / 32, styles late | 601184 | 2183 | 66784 | 1059 |

Stored and deflated members retain the same lookup counts. Warm CSS-cache calls add zero archive reads. Fifty-four original/repaired runs produce byte-identical real CSS cache files. An optional miss can add one bounded initial pass before the original cost; no improvement is claimed for those fallback workloads.

The local Springboard aggregate, package-source version guard, syntax/whitespace and a 32-bit Xtensa ZipFile translation-unit compile passed. Independent final source review and strict normal/ASan rerun passed. NativeApp aggregate and hosted target/exact-head checks are tracked in the PR and terminal coordination comment; this committed checkpoint is not a claim of their completion.

Limits: local LeakSanitizer is unavailable under ptrace and disabled only by the calling environment; hosted defaults remain enabled. UBSan is intentionally absent from this focused ZIP suite because existing BUG256 separately owns unaligned EOCD loads. No broad ZIP-correctness repair, full local firmware build, physical SD/device timing, heap-peak measurement or hardware/visual qualification. The actual parser output-equivalence evidence supports unchanged rendering inputs; no screen capture was made. BUG210's existing partial-cache-on-I/O-failure behavior remains separate.

## Coordination

Fresh metadata covered all 399 PRs, 349 branches and 50 queue comments. The prior scanner's exhaustive source comparison was extended to new heads PR399 `bc243816` and protected Grok PR384 `4785443e`; relevant CSS/ZIP functions remain unchanged there. PR396's TOC and PR399's language claims are released. The shared ledger remains `13f6c55cf482738f583efc58cd922e0afe835108` with its recorded writer unchanged. No canonical allocation, competing ledger writer, owner/Grok branch mutation, default-branch write, merge, release, deployment or device action.
