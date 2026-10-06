# Model Viewer binary STL loading performs one storage call and HAL wait per triangle

## Stable identity and source

- Repository-qualified stable ID: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261006-MODEL-STL-RECORD-READ-WAITS`.
- Repository ID: `1367546328`. Scan started `2026-10-06 06:40 UTC`.
- `source_ref` / intended repair base: `xteink-x4-pro-boot`; latest revalidated `baseline_sha`: `1cb042921630b7d2f6613c790ee546cf4c26c560`; source [PR350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350).
- Default branch is `master`, finally verified at `c765a9931e2f1772d1dd3b5870362c18a08a9e57` after the owner merged PR414. PR350 remains open/unmerged; its comparison with master is diverged, with 219 PR350-only and 31 master-only commits. PR348 remains closed-unmerged at `108daf2050065076346ae016b3b030b1e7ec3e2a`, an ancestor of the PR350 lineage. This report does not represent master integration.
- The scan initially measured `f14196766af6c3bc370d1755d7d523a5bad5650d`. PR350 advanced during the scan for the separately owned BMP row-wait repair. All 115 enumerated measured sources, dependency headers and fixture sources are byte-identical at the new head; their Git blob and SHA256 hashes are preserved beside the reproduction.
- Report only. Canonical number, implementation claim, repair branch/PR and version reservation remain unassigned. Model Viewer stays `1.2.8`.

## Reachable production workload

Select a valid binary `.stl` through the ordinary File Browser association and open it in installed Model Viewer. Its manifest declares `.stl` support and requires `display.output` and `input.touch.raw`. After its loading frame, the unchanged `app_main()` dispatches the handed path through `mv_load_model()`, checks the binary header and loads every triangle before rendering the mesh.

The app explicitly requires a 960×540, stride-120 MONO1 display surface. This is the T5-compatible Model Viewer path with its required providers installed. The X4 display-size guard rejects the ordinary X4 surface before parsing, so **this finding does not claim X4 Model Viewer reachability**, an installed app/provider on the owner's device, or a hardware reproduction.

The fixtures contain ordinary finite triangle coordinates and exact valid binary STL lengths. No malformed record, error, slow card, long text line or excessive triangle count is required. The main example has 10,000 triangles, a 500,084-byte file and 360,000 bytes of triangle payload storage, well below the app's declared 80,000-triangle bound.

## Root cause

`mv_reader_t` already owns a 768-byte buffer. The text readers use it through `mv_reader_getc()`. Binary STL parsing instead calls `mv_reader_exact(record, 50)` for each triangle. That helper bypasses the buffer and forwards every 50-byte request directly to `g_storage->stream_read`, then clears the buffer's cursor state.

`NativePlatformBridge::streamRead` forwards each call to ordinary `HalFile::read`. On the T5 provider-volume backend selected by this PR350 lineage, every successful read goes through the storage lock, handle/media/error checks and `delay(1)` in `HalFile::readWithBudget`'s null-budget path. Therefore even a healthy sequential file whose sectors are already handled by FatFs still makes N tiny stream requests and N HAL delay requests for N triangles. The two separately read 84-byte headers add two more.

The app's existing parse service call every 512 records does not amortize these HAL calls. The report concerns avoidable storage/API/scheduling overhead during loading, not triangle rasterization, the separate input-cancellation semantics, or total device responsiveness.

## Deterministic evidence

The reproduction compiles the **complete unchanged `Apps/model_viewer.c`**, including its actual parsing and rendering helpers. It links the complete unchanged production `mapStoragePath`, `streamOpen`, `streamRead`, `streamSeek` and `streamClose` bodies with the unchanged T5 SD driver, storage-volume implementation, FatFs and `HalStorageVolume.cpp`.

The existing SPI-card fixture supplies a formatted 64 MiB FAT32 card image. The generated STL is written and closed through the real production storage stack before counters start. In the complete-app mode, the actual `app_main()` obtains its file-handoff path, presents the loading frame, loads and validates every generated coordinate, successfully acquires the modeled touch provider, rasterizes and submits the first mesh frame, receives Back, and performs its normal cleanup. Exactly two frames are submitted and both display/touch leases and the touch subscription are released once. A separate direct-loader mode independently verifies every parsed triangle and frees the model.

All four execution modes, direct-loader normal and fatal ASan/UBSan plus complete-app normal and fatal ASan/UBSan, produce identical records across these five workloads, **20 successful scenarios**:

| Triangles | STL bytes | 50-byte reads | Total stream reads / HAL delay requests | Storage mutex acquisitions | Card sector-read requests |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 134 | 1 | 3 | 19 | 2 |
| 512 | 25,684 | 512 | 514 | 1,041 | 53 |
| 1,000 | 50,084 | 1,000 | 1,002 | 2,017 | 100 |
| 10,000 | 500,084 | 10,000 | 10,002 | 20,017 | 986 |
| 80,000 | 4,000,084 | 80,000 | 80,002 | 160,017 | 7,876 |

The 10,000-triangle case reads 500,168 bytes, exactly the file length plus the repeated 84-byte header; it makes 20 app parse-service polls. Each case opens once, seeks twice, closes once and finishes with no stream handle, no held storage mutex, quiescent HAL generation, and successful provider shutdown. Model coordinates, triangle count, bounds and positive normalization are checked. Full-app execution also checks its source cleanup after Back.

Importantly, **10,002 storage reads are not 10,002 physical sector reads**: the real FatFs cache reduces the example to 986 modeled sector-read requests. The much larger count persists above the filesystem cache because the app still enters the storage ABI/HAL and requests a delay for every record.

The SD electrical transport, clock, allocator, provider discovery, display surface/presentation and user input are host fixtures. Rasterization and parser/storage algorithms are production; frame acceptance is modeled rather than a physical panel. Clock accounting does not model host rendering CPU time or establish target task scheduling. `delay(1)` counts must not be read as milliseconds of actual latency: tick conversion, scheduler contention and card/panel timing are platform-dependent. No physical load time, device-speedup percentage, battery effect, target firmware build or owner-device installation is claimed. LeakSanitizer is disabled under the tracing environment; ASan/UBSan remain fatal.

Reproduce from this checkpoint's source tree:

    python3 docs/performance/evidence/model_stl_record_read_scan.py
    python3 docs/performance/evidence/model_stl_record_read_scan.py --sanitize
    python3 docs/performance/evidence/model_stl_record_read_scan.py --app
    python3 docs/performance/evidence/model_stl_record_read_scan.py --app --sanitize

`--source /path/to/source` can explicitly select the immutable source tree. The script writes only an isolated evidence-build directory and does not patch production source.

## Impact and repair direction

A normal Model Viewer open incurs avoidable tiny-call and scheduler-request overhead proportional to the triangle count before its first mesh frame. At 10,000 triangles, roughly 500 kB of sequential input crosses the native stream/HAL boundary more than 10,000 times even though the parser already has a reusable chunk buffer. At the accepted upper bound this reaches 80,002 stream calls and delay requests.

Use a bounded buffered exact-read path that consumes the existing reader buffer and refills in useful chunks, or another narrowly scoped chunked STL reader. Parse multiple 50-byte records from each refill. Preserve logical offsets across reads and seeks, binary/ASCII dispatch, size/count/overflow and finite-coordinate validation, short/error handling, model cleanup and the existing public storage ABI. Retain real cooperation with both item/byte and elapsed-time checkpoints rather than suppressing all yields globally. Validate ordinary, boundary-crossing, truncated, short-read, seek/retry and cleanup cases before claiming a repair. No fix or changed behavior is included here.

## Deduplication and ownership

The census inspected 387 branches, 435 pull requests across all states and 581 distinct current heads before this report's publication. All eight distinct `model_viewer.c` variants containing the app retain the direct per-record `mv_reader_exact` call. All four bridge variants with `streamRead` forward to ordinary `HalFile::read`; the fifth predates that API. All four provider-HAL variants retain per-read delay calls. The final PR350 source delta was revalidated separately when the BMP repair advanced its head; the subsequent PR414 merge on master leaves the relevant Model Viewer and stream code unchanged.

The current canonical `bugs.md`, `docs/BUG_FIX_PROGRESS.md`, `docs/BUG_FIX_WORKFLOW.md`, all 186 PR332 queued reports/claims through comment 6010952611 and PR titles/bodies were inspected. The historical report census covers 257 distinct blobs, including the one legacy non-UTF8 blob read byte-preservingly. Three blobs absent from the local object store were read by exact blob SHA through the GitHub connector. No matching report or repair was found; the live non-PR issue query returned no issues.

Canonical #28 concerns a first-frame submit race, #135 failed touch-acquisition cleanup, and #136 long textual OBJ-record truncation. This binary healthy-input cost requires none of those failures. The prior BMP row-wait finding and its current repair operate in `Bitmap`/`GfxRenderer`; the latest `1cb0429` repair leaves the Model Viewer parser, stream ABI and ordinary HAL reads unchanged. File Browser resume/path, image probe, native text layout, Open-with registry, text/cache and provider-discovery reports and repairs remain separate.

The recorded canonical coordinator remains the single ledger writer. This isolated documentation/evidence checkpoint is submitted to the existing PR332 reconciliation queue without editing the shared ledger, renumbering reports, claiming a repair or changing another owner's source. No production changes, master writes, merge, auto-merge, release, deployment, flashing, device operation, CI dispatch or schedule changes were performed.

## Immutable source anchors

- [Buffered text reader and unbuffered exact reader](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Apps/model_viewer.c#L194-L246)
- [Binary STL validation and per-triangle loop](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Apps/model_viewer.c#L452-L492)
- [Actual application surface gate and handoff/load path](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Apps/model_viewer.c#L1167-L1227)
- [Native stream forwarder](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/src/native/NativePlatformBridge.cpp#L157-L184)
- [Ordinary provider-HAL read and delay](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/lib/hal/HalStorageVolume.cpp#L272-L296)
- [Model Viewer package requirements and associations](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Apps/model_viewer.json)
