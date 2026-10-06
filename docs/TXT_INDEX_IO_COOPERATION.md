# TXT page-index cooperation

Source-qualified issue: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-TXT-INDEX-SCALAR-IO-WAITS`.

- Source/base: `xteink-x4-pro-boot`, `36891e71ab651152c618d60d1d248abb4a7f44ff` (PR #350, unmerged).
- Report: [PR #332 comment 5994999903](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5994999903).
- Claim: [PR #332 comment 5995405815](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5995405815).
- Repair: `perf/txt-index-io-budget`; firmware **1.3.128 → 1.3.141**, accounting for reservations through 1.3.140. No app/driver/provider payload changes.

## Trigger and focused change

An ordinary TXT reopen reaches `TxtReaderActivity::loadPageIndexCache()` before displaying the requested page. Its unchanged 30-byte header and N uint32 offsets use N+9 scalar reads. Both X4 and T5 provider-volume HALs previously requested one scheduler wait for each successful scalar. Saving has the same scalar pattern. The original production-source probe confirms 1,009 reads/waits for 4,030 bytes at 1,000 pages and 10,009 reads/waits for 40,030 bytes at 10,000 pages.

The two cache methods now explicitly own one read or write cooperation budget, only for `BOARD_XTEINK_X4_PRO` and `BOARD_T5S3_PRO`. The HAL implementation is the union of the existing read primitive in PR #425/#417 and write primitive in PR #426; both budget headers are byte-identical to those implementations. No TextBlock/image caller from those separate repairs is included or changed.

A budget cooperates after 32 successful provider operations, 4,096 bytes, or eight elapsed milliseconds, whichever comes first. Real `vTaskDelay(1)` performs the yield. Before/after checkpoints cover intervening CPU work and unsuccessful calls; unsigned elapsed arithmetic handles clock wraparound. Budget state is local to one method invocation. There is no buffering, prefetch, persistent handle state or resource-size increase.

## Preserved contracts

- Exact scalar request sizes/order, bytes, endian behavior, cache magic/version, header eligibility, size rejection and offset sequence.
- Original provider calls, per-call storage locks, mutation invalidation, sync boundaries, short/error returns, destructor-close and subsequent retry behavior.
- HAL 16 MiB request limit, 4,096-byte provider chunk limit and 20-second per-call deadline.
- All ordinary HAL and serialization callers; non-volume TXT continues ordinary serialization/scheduling.
- Reader defaults, layouts, navigation, error/recovery behavior and existing U1 safety/coherency. No UI or physical raster changes.

This is a scheduling-cost repair. The N+9 provider requests and locks remain. It does not fix historical unchecked scalar reads/writes, stale same-size cache validity (BUG #101), or add new cache validation. Undefined zero/partial scalar-reader cases must not be used to claim parity or a fix. No actual device latency, SD sector count, FPS or hardware success is asserted.

## Verification

The focused `test/activities/txt_index_io_budget_test.py` executes extracted production cache methods, full serializers and production volume HAL read/write bodies. The provider/clock/open/size/close boundaries are fixtures. It compares deterministic original/repaired data and request traces, exercises defined rejection/fault/retry/cleanup cases, and checks byte/item/time cooperation. At 1,000 pages, both loading and saving request **31 waits instead of 1,009**, while keeping all 1,009 I/O requests and 4,030 bytes. At 10,000 pages, waits are **312 instead of 10,009**. The full original/repaired 5,773,325-byte trace snapshots are identical. These are deterministic host operation counts. It also provides an original-cost negative control and non-volume caller control.

The original fragments are checked in as a SHA256-pinned fixture for shallow/offline checkouts. `--verify-baseline-git` additionally checks that fixture against the complete original Git blobs; it is not a network-dependent CI prerequisite.

The NativeApp host aggregate runs the new regression with ASan/UBSan plus the legacy caller control. Local LeakSanitizer is disabled because ptrace prevents it; ASan/UBSan remain active. Existing real X4 and T5 SPI provider/FatFs/HAL tests separately cover storage error and lifecycle behavior. Exact-head hosted build results are recorded in the repair PR when available; local firmware links and physical/device tests are not claimed. The harness uses host-width size_t; malformed page-count arithmetic is not target-width validation, and no new allocation cap or overflow repair is asserted.
