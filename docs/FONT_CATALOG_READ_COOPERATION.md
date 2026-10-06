# Font catalog scalar-read cooperation

Issue: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-FONT-CATALOG-BYTE-READ-WAITS`.

- Repository ID: 1367546328.
- Source/ref and PR base: `xteink-x4-pro-boot`, baseline `36891e71ab651152c618d60d1d248abb4a7f44ff`, [PR #350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350), still unmerged.
- [Source-qualified report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5997210080) and [repair claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5997373605).
- Repair branch: `perf/font-catalog-read-budget`.
- Firmware **1.3.128 → 1.3.142**, beyond live reservations through 1.3.141. No app, driver or provider payload changes.

## Reproduced cost

Manage Fonts calls the firmware's `refreshCatalog()` synchronously before its first list render and input poll. ArduinoJson 7.4.2 consumes `HalFile` through its generic scalar reader. Each successfully consumed byte therefore causes one provider request and one volume-HAL `delay(1)` invocation.

An accepted synthetic catalog with 64 families, four valid files per family and 127-byte descriptions is 26,259 bytes. The original production app, bridge, validators, X4/T5 SD providers, FatFs and HAL execute all **26,259 scalar reads and waits before first render/input**. This workload fits the app's existing 64-row capacity. A normal 1-family catalog uses 347 reads/waits; 16 families use 4,547; 64 compact families use 18,131. The registry is modeled without installed fonts for these measurements. These are host operation counts, not elapsed hardware times or official release-asset measurements.

## Focused implementation

Only the X4/T5 `refreshCatalog()` parser uses a stack-local `CatalogReader`, whose scalar `read()` keeps the original one-byte result and error contract. It reuses the existing `HalReadBudget` and `readCooperatively` implementation from PR #417/#425. The four read-support HAL files are byte-identical to PR #425 at `b7e1eeafe85f584c3d6e32c610047be7d9e086de`. They are included because that support is not yet integrated into PR #350; no separate repair branch is adopted as this PR's base.

Cooperation occurs after 32 successful provider reads, 4,096 bytes or eight elapsed milliseconds, whichever comes first, using real `vTaskDelay(1)`. Before/after checkpoints include CPU gaps and unsuccessful calls. Unsigned elapsed subtraction handles clock wraparound. The budget is new for each refresh and has constant memory. Existing 4,096-byte provider chunking, 16 MiB per-call limit and 20-second per-call deadline remain unchanged.

The optimization reduces scheduler requests, not provider I/O. There is no read-ahead, buffered tail, new persistent state, new catalog cap or timeout expansion. A bulk JSON read was deliberately avoided because a provider can copy bytes and then report an error; bulk prefetch would move the parser's failure boundary and could also read beyond an already accepted root.

## Preserved behavior

- Exact scalar provider request sizes/order, bytes, lock boundaries and EOF/error semantics, including sticky errors and ignored unread tails.
- Unchanged JSON schema/name/filename/CRC-type validation, duplicate rejection, candidate publication and prior catalog/base URL preservation after rejection.
- Unchanged installed-font size/CRC checks, registry refresh, native app, row ordering, labels, actions, output defaults and error/retry paths.
- Unchanged close/remove order and handling of their return values. No new post-parse error gate or tail-consumption check.
- Legacy `deserializeJson(doc, file)` and all ordinary HAL callers retain their scheduling. The budget does not extend to installed-font CRC reads.

Removing the new private adapter/include and the guarded parser opt-in reconstructs the exact original `NativeFontBridge.cpp`; no other bridge function changed. Driver, application and SDK public ABI sources are unchanged. This preserves the existing 1.3.53 UX/function/visual contract and U1 safety/coherency requirements within this performance-only change; physical raster and hardware latency are not claimed as tested.

## Regression and limits

`test/native_apps/font_catalog_cooperation_test.py` uses the checkout's production definitions and existing storage-volume fixture, with the pinned ArduinoJson dependency supplied as its path argument. Hosted firmware CI invokes it on both SD transports. No broad historical source snapshot or denied full reproducer is published.

The production shared-clock fixture on **both X4 and T5** keeps all 26,259 one-byte reads and reduces HAL waits **26,259 → 821** before the actual app's first render/input. Provider sleeps are 53 in both launch runs. An isolated budget-only unit produces the ideal 820 yields; the integration's extra yield is a genuine elapsed-time checkpoint. No independent clocks are used to force the integration count. The cooperative run is repeated and requires byte-identical full reports, including its schedule.

All **48** cases compare complete catalog/base URL, familyInfo and app rows plus storage request positions, returned bytes, errors, cleanup and modeled sector counts. Coverage includes malformed/truncated/NUL/UTF-8/escaped JSON, ignored tails with unread faults, invalid schema/names/CRC/duplicates/empty files, prior-state preservation, valid retries, a copied final delimiter followed by a sticky error, open/network/media/unavailable failures, close/destructor and removal retries, and installed-file size/CRC match/mismatch/missing/read-error branches. Injected per-read cost and clock wraparound exercise elapsed cooperation; a failed no-progress call still checkpoints. Only scheduling is excluded from original/repaired semantic comparison.

The optional `--source` and `--original-hal` arguments additionally compile locally supplied original files. The full pinned original bridge and HAL from baseline 36891e71 are checked against Git and the earlier source-qualified probe before use. The legacy guard is compiled against the volume test fixture to verify the unchanged parser call; it is not a runtime test of legacy SdFat. Normal/UBSan and ASan/UBSan results and exact-head hosted checks are recorded in the PR. The existing complete NativeApp and Springboard aggregates, both real storage-volume suites, storage admission and package-version guard also run separately.

Host OS/scheduler, card wires/registers, network input, registry and UI callbacks are fixtures. Actual downloader networking, physical media faults, installed-ELF loading and hardware/UI timing remain unrun. LeakSanitizer is disabled in the tracing environment; ASan/UBSan remain enabled. Exact-head check results are recorded in the PR after execution. No merge, release, deploy, device write or flash is authorized by these tests.
