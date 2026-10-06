# File Browser repeats complete directory scans when returning from a file handler

## Stable identity and source

- Repository-qualified stable ID: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261006-FILE-BROWSER-RESUME-REDUNDANT-SCANS`.
- Repository ID: `1367546328`. Scan: `2026-10-06 05:40 UTC`.
- `source_ref` / intended repair base: `xteink-x4-pro-boot`; exact `baseline_sha`: `f14196766af6c3bc370d1755d7d523a5bad5650d`; source [PR350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350).
- Default branch is `master`, checked at `21ce3b5b720e106815c8f3a7778bc7003294e0e2`. PR350 is open/unmerged and 218 commits ahead of master. PR348 is closed-unmerged; its head `108daf2050065076346ae016b3b030b1e7ec3e2a` is an ancestor of this baseline, not a master integration.
- Report only. Canonical number, implementation claim, repair branch/PR and version reservation remain unassigned. File Browser remains version `1.3.4`.
- Affected production code: `Apps/file_browser.c::app_main`, `load_session`, `load_files`, `consume_handoff_results`, and the `open_with_handler` session/handoff path. `src/native/NativeAppHost.cpp::dirOpen/dirNext/dirClose` supplies the directory operations.
- File Browser source blob: `d43cf776e8a4862fd7ee8d1682ad6581f84d3fdd`, SHA256 `5e281d994b6d7770db067ee146e6428a22eac4a7835e67e9a4b997d434c32e39`.

## Production reachability and root cause

Open an associated image from the ordinary File Browser and return from its app. `open_with_handler` saves the actual current directory and selected filename, requests the file handler, and exits. `NativeFileOpenActivity::loop` runs the handler and then calls `runNativeApp` for the saved File Browser executable. Its result accessor only copies the error/cookie and clears the result; it does not change the directory.

On every resumed File Browser invocation, `app_main` initializes the path to `/` and performs these operations before its first browser frame or input poll:

1. `load_files(NULL)` enumerates and sorts the root directory.
2. `load_session()` reads the successfully saved session, restores the saved path and calls `load_files(saved_name)` again.
3. `consume_handoff_results()` consumes the file-handler result, obtains the selected name from that fresh listing, and calls `load_files(selected)` yet again.

When the saved path is `/`, the same unchanged root is enumerated three times. For a nested path, the unrelated root is scanned once and the target directory twice. The extra initialization and result refresh are synchronous foreground work, not useful intervening observation: no browser frame is presented between them, and this successful file-handler result accessor performs no filesystem mutation. A fresh post-handler listing is appropriate; the two additional listings are unnecessary.

This orchestration defect exists independently of how efficiently each directory entry is obtained. Each complete scan also repeats sorting and rebuilding the display-entry array. Current provider-backed `dirNext` opens each actual child, so the repeated listings multiply parent-path lookup work as well. This report does not count the underlying child-open implementation as a second new defect.

## Deterministic host evidence

The attached reproduction script compiles the complete unchanged `Apps/file_browser.c` and the complete unchanged production `storagePath`, `dirOpen`, `dirNext` and `dirClose` bodies. It links the production X4 SD driver, storage-volume provider, FatFs and HAL against the existing native SD-card wire fixture.

The fixture formats a 64 MiB FAT32 image and creates one `Books` directory plus N small valid 24-bit BMP files at the root, and N BMPs inside `Books`. The real production `NativeImage::readBmpLayout` validates each generated BMP. Names, component counts and directory counts are within existing production bounds; the largest root has 201 entries, below File Browser's 256-entry limit. Image payload size is immaterial to listing, and no file content is read by directory enumeration.

The first complete app invocation uses ordinary row-selection/Open events, optionally enters `Books`, selects `image000.bmp`, and invokes its real save-session/handoff logic. Session bytes are the actual output of `save_session`, stored by an in-memory session-storage callback. The file-handler boundary is a fixture reporting one Image Viewer association and successful completion. A second complete app invocation consumes that result, renders the restored directory once, receives Exit, and performs its actual session/Back-exit cleanup. It does not execute Image Viewer, map its ELF, or present a physical screen.

At the first resumed browser frame, all cases have exactly three successful native directory opens, three closes and no remaining directory handle. The returned selection is the original image. Exact sector read requests are:

| Files N in each location | Root resume: three root scans | Nested resume: root + Books + Books |
| ---: | ---: | ---: |
| 8 | 0 + 0 + 0 = 0 | 1 + 17 + 18 = 36 |
| 32 | 64 + 64 + 64 = 192 | 64 + 110 + 110 = 284 |
| 64 | 268 + 268 + 268 = 804 | 268 + 346 + 346 = 960 |
| 128 | 1,060 + 1,060 + 1,060 = 3,180 | 1,060 + 1,202 + 1,202 = 3,464 |
| 200 | 2,564 + 2,564 + 2,564 = 7,692 | 2,564 + 2,778 + 2,778 = 8,120 |

The eight-file root remains in the real FatFs sector cache, so its extra enumeration produces zero additional sector reads. It still performs three scans, 30 `dir_next` calls including end-of-directory, and 27 returned entries. The report preserves this small-workload control rather than implying all repeated scans reach storage.

For N=128, a root resume makes 390 directory-next calls and returns 387 entries, although the visible directory has only 129 entries. The nested resume makes 388 calls and returns 385 entries for a 128-entry target listing. At N=200 the corresponding counts are 606/603 and 604/601. These counts and every table entry agree exactly between the normal and fatal AddressSanitizer/UndefinedBehaviorSanitizer executions: ten root/nested scenarios in each build. Both app invocations restore Back-exit behavior; session removal, result consumption, storage-mutex release, quiescent HAL generation and final provider shutdown are asserted.

Sector counts are requests from the actual production stack to a modeled card, not physical SD latency, transferred image bytes, a device-speed measurement or a promised percentage reduction in total resume time. Panel rendering, session-file I/O, association lookup, handler execution, ELF loading and the parent's app lifecycle cost are outside these counters. Physical hardware, installed user inventory and target builds were not tested. LeakSanitizer is disabled for the traced executor; ASan/UBSan remain fatal.

Reproduce from this checkpoint's unchanged-source tree:

    python3 docs/performance/evidence/file_browser_resume_scan.py
    python3 docs/performance/evidence/file_browser_resume_scan.py --sanitize

The script creates only its isolated evidence-build directory. It does not patch production code. The neighboring normal/sanitized logs preserve the final valid-BMP workload, and `file_browser_resume_source_hashes.json` identifies all measured source files.

## Impact and focused repair direction

An ordinary return to File Browser repeats costly filesystem enumeration before the user can interact with it. A folder of 128 visible images causes 2,120 avoidable root-sector requests beyond one of its identical root scans, or an unnecessary root scan plus a duplicate target scan when resuming inside `Books`. This is in addition to existing app-launch and display costs, not an explanation of all device latency.

Restore and validate the saved path/selection before choosing the initial directory to enumerate. For a file-handler or raw-ELF return that only consumes a result/status, reuse the single fresh resumed listing instead of scanning it again. Do not introduce a persistent stale directory cache, remove required refresh after actual rename/delete/move mutations, change stored identity or skip validation. Preserve no-session fallback, failed directory/session reads, selection semantics, result cookies, USB lifetimes, failure/retry and teardown. Add full app handoff/resume tests for root/nested directories, unchanged and externally changed contents, success/error results, missing/invalid sessions and genuine mutating continuations. Existing separate rename-safety defects must not be silently bundled into this performance report.

## Deduplication and integration reconciliation

Read the root/platform/cooperative contracts, authoritative `bugs.md`, `docs/BUG_FIX_PROGRESS.md`, `docs/BUG_FIX_WORKFLOW.md`, current performance reports and all 181 queued PR332 comments. The live census contains 386 branches, 435 PRs across all states and 581 distinct branch/PR heads, with no non-PR issues. All-state PR titles/bodies, current claims and ordinary repair work were checked. No other performance scanner is active; the native text repair owner confirmed its claim was released and no overlapping File Browser repair was owned.

Reused the prior scan's immutable source mapping, then checked the three new heads separately. All eight historical File Browser variants retain the eager root listing followed by session restoration; seven include the later app-association return and its extra reload. The earliest variant predates that association API and is not a repair. Current source is the unchanged `d43cf...` variant. The current/new heads contain no alternate repaired initialization sequence.

Historical reconciliation inspected 253 distinct report/ledger/README blobs and 73 distinct relevant excerpts, plus the current queue and PR descriptions. One historical `bugs.md` blob is non-UTF-8: the connector's explicit base64 file route returned readable normalized text, which was searched but is not represented as a byte-identical original. Its six relevant sections concern the already-known destination-picker, association-limit, firmware handoff, viewer-race, directory-cap and Ask-session bugs, not this defect. The exact original blob identity remains recorded in the scan checkpoint.

Canonical BUG-105 and BUG-172 concern wrong-target rename after stale/missing session identity; this workload has a valid successful session and no rename. BUG-59 concerns directories beyond 256 entries; this workload remains below that limit. The Open-with report `6007670030` concerns registry child opens, and path-fitting report `6009597793` concerns font measurement during redraw. This finding invokes neither registry rebuild nor text-width measurement in its counters. PR408's app-inventory probe repair and the HAL child-stat repair do not alter this app's three-listing control flow.

The native text-view reflow report `6008603467` is already repaired in exact baseline `f141967...`, as are the integrated keyboard and TXT paragraph-boundary changes. Their source/history is preserved; they are not rediscovered. The broader TXT remeasurement issue remains distinct from its narrower boundary reuse repair. Master still lacks this working-line integration.

The authoritative ledger coordinator remains on `automation/bug-ledger` at `101e2f7f58a8daabbccd3227ad878105e3847320`. This immutable per-issue checkpoint is queued through existing PR332 for reconciliation, with no second report PR and no direct ledger-file write. No implementation fix, PR350/master write, merge, auto-merge, release, deployment, flash, hardware or schedule change occurred.

## Immutable production anchors

- [Startup ordering](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/Apps/file_browser.c#L1057-L1068)
- [Session restoration](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/Apps/file_browser.c#L306-L340)
- [File-handler handoff](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/Apps/file_browser.c#L705-L727)
- [Result-driven reload](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/Apps/file_browser.c#L997-L1014)
- [Actual resume controller](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/src/native/NativeFileOpenBridge.cpp#L59-L102)
- [Side-effect-free result accessor](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/src/native/NativeFileOpenBridge.cpp#L147-L154)
- [Production directory bridge](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f14196766af6c3bc370d1755d7d523a5bad5650d/src/native/NativeAppHost.cpp#L411-L450)
