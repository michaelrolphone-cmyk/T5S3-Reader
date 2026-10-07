# TXT indexing: retain the exact page read window

## Identity and scope

- Stable report: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-TXT-INDEX-OVERLAPPING-READS`.
- [Original report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5988326088), [scoped repair claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5988488287).
- Source and intended target: `xteink-x4-pro-boot`, PR350 at `36891e71ab651152c618d60d1d248abb4a7f44ff`. Firmware **1.3.128 → 1.3.129**. No app/driver/provider payload changes.
- Default master remained `21ce3b5b720e106815c8f3a7778bc7003294e0e2`; PR350 is open, PR348 closed without merge. PR415's font repair is integrated into PR350, not master. No claim of X4 integration into master.
- Separate branch `perf/txt-index-sliding-reader`. Shared coordination files and other repair/owner branches are unchanged. No merge, release, signing or device operation.

The live inventory contained 415 PR heads, 364 branches and 558 distinct heads. Compared with the report's all-state source/122-ledger-blob comparison, only PR350 advanced from76fc8b6d to36891e71; the report verified all14 relevant caller/storage blobs unchanged. Refreshed PR332 claims, all-state TXT/readContent/loadPageAtOffset searches and changed paths of every current independent open PR found no duplicate repair. The title-truncation, percentage-jump and repeated-image reports remain separate.

## Change

Cold TXT and Markdown indexing repeatedly ask the existing page parser for an8KiB-or-EOF window. Typically a page consumes much less. Previously every request opened/seeks/read/closes a new file, even for bytes just read by the prior page.

`Txt::ReadWindow` is scoped to one `buildPageIndex` call. It owns one file and one allocation of `min(fileSize,8192)+1` bytes, no more than the existing initial page buffer. It retains the exact unread suffix, moves it down and reads only the new tail. Forward indexing does one initial seek; shorter, repeated, backward and disjoint requests still use exact cursor accounting. There is no whole-document cache, persistent reader handle, font/layout change, new cache format, per-device path or timeout change.

The existing page parser receives byte-identical windows, including its NUL terminator and complete SD-font preparation input. Text lines, Markdown fences/headings, CRLF, UTF-8 boundary behavior, offsets and wrapping policy are unchanged. Independent random page rendering continues using its original `Txt::readContent` path. Existing BUG-157 short-read behavior in that legacy path is not claimed repaired.

The optimized reader admits only complete successful reads and checks observed storage-generation coherence before/after reads, including fully cached suffixes and the final checked close. An observed mutation, unavailable/nonquiescent storage, open/seek/read/allocation/close error refuses that operation instead of reusing uncertain bytes. Every failure/exit closes through the existing HAL ownership rules; a failed close retains/poisons the provider as before. A new operation can retry when storage is usable. No failed/partial index is persisted: `buildPageIndex` now returns completion and `initializeReader` saves only a completed index. Existing in-memory partial-page behavior after an indexing failure remains; reopening the reader retries because no successful index cache was written.

Work is bounded by the existing loaded file size and strictly increasing page offset, an8KiB maximum window/read request, and the provider's unchanged I/O bounds. Indexing retains the eight-page scheduler checkpoint and adds an elapsed20ms checkpoint with an actual one-tick yield, including counter rollover. Existing wrap-step yields remain. There is no new book-size cap, timeout inflation, unbounded retry, new cancellation UI or weakened validation.

## Reproduction and regression

`ASAN_OPTIONS=detect_leaks=0 python3 test/txt_index/index_test.py [--sanitize]`

The harness compiles the actual Txt header and extracted verbatim production constructor/load/read/window methods; unchanged production page parser and actual buildPageIndex; actual Markdown, HalStorageVolume, X4 SD provider and FatFs implementations; and the existing SD register/wire fixture. Font metrics and activity/UI boundaries are modeled. The source-audited production `ensureSdCardFontReady` delegates to `SdCardFont::buildAdvanceTable`; all six storage opens in that implementation are read-only. A real concurrent read-only HAL handle test confirms those reads do not invalidate the window generation. Actual font preparation is not executed by this fixture. The test also compares every page line/style/next-offset/fence and every full SD-font preparation string against the unchanged random-access parser path.

For a newline every32bytes and32lines/page:

| File | Pages | Original payload | Window payload | Original sectors | Window sectors |
|---|---:|---:|---:|---:|---:|
|64KiB|64|495,616|65,536|1,103|131|
|128KiB|128|1,019,904|131,072|2,327|260|
|256KiB|256|2,068,480|262,144|4,967|518|
|512KiB|512|4,165,632|524,288|11,015|1,034|

Opens/seeks fall from the number of pages to one. The original payload formula is `8192*pages-28672`; the repaired payload equals file size. Counters include the indexing file open/seek/close but exclude fixture creation/load. The report's original sector count is one higher because this regression loads Txt through production load first, warming metadata; its sequential control excludes the initial open. Neither difference changes the redundant-payload result. Sector counts apply only to the specified512-byte-sector, one-sector/cluster model, not every SD card.

`--original` uses the unchanged per-page read path in the extracted production builder and must fail the one-pass payload assertion. It is a negative control, not an alternate implementation.

Covered: ASCII, empty lines, CRLF, no final newline, long lines crossing8KiB, UTF-8 around a window boundary, Markdown headings/fences/chapter boundaries, SD-font preparation windows, repeat/backward/disjoint/EOF reads, invalid ranges/unloaded input, tiny/empty allocation parity, slow/rollover elapsed checkpoints, read-only handle coexistence, allocation/open/seek/zero/short-read errors, operation retry, generation changes with cached bytes and after the last page, active unrelated writer, checked close failure/retained ownership, exact allocation cleanup. The test is included in the existing Springboard host aggregate.

## Verification status

- PASS: initial normal and ASan/UBSan production-stack tests and output equivalence; original-cost negative control fails as expected; full NativeApp and Springboard aggregates; changed-package version guard and whitespace checks.
- In progress at initial publication: final focused rerun after tiny-file allocation refinement, independent review and exact-head hosted firmware/CI checks. Consult the PR for the latest verified commit and results.
- Unrun locally: full firmware link; PlatformIO is not installed in this native executor. Hosted target builds are required before readiness.
- Not claimed: physical SD timing, device latency/FPS, framebuffer/font rendering qualification, cancellation responsiveness, or hardware operation. LeakSanitizer is disabled only for the local ptrace limitation; AddressSanitizer and UndefinedBehaviorSanitizer remain active. Hosted execution uses its normal sanitizer defaults.
