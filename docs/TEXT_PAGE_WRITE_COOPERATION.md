# Text-page write cooperation

Issue alias: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-TEXT-PAGE-SERIALIZATION-WAITS`.
[Report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5993941360),
[claim / firmware 1.3.140 reservation](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5994447527).

## Source and scope

Source ref and repair base `xteink-x4-pro-boot`, exact parent
`36891e71ab651152c618d60d1d248abb4a7f44ff`. PR350 is open/unmerged;
master is `21ce3b5b720e106815c8f3a7778bc7003294e0e2`, and PR348 is
closed-unmerged. Repair branch `perf/text-page-write-budget`. The integration
record and final PR/head/checks are published in the PR332 discussion.

The fresh 425 all-state PR heads and 374 branches exactly match the report's
568-head source/report inventory. Current queue, canonical inventory/progress,
historical reports, writer function bodies and issue search contain no competing
serialization repair. PR425 changes deserialization and remains separately owned;
this branch does not incorporate its read optimization. Adjacent HAL/header and
serializer additions need to be combined at integration without dropping either
caller. No shared ledger or owner/default branch is changed.

Firmware `1.3.128 → 1.3.140`, above published tags and reservations through
1.3.139. No independently distributed app/driver/service/provider, ABI, manifest,
page-cache version or SD package payload changes.

## Production change

Cold chapter creation calls `Section::onPageComplete → Page::serialize →
PageLine::serialize → TextBlock::serialize`. Each field/string was an ordinary
HAL write; the provider-volume adapter requested `delay(1)` after each successful
provider chunk even for one-byte fields.

Only the TextBlock writer on X4/T5 provider-volume builds opts into one explicit,
stack-local `HalWriteBudget`. It checkpoints after 4096 successful bytes, 32
successful provider chunks, or 8 elapsed milliseconds, and calls `vTaskDelay(1)`.
Pre/post checkpoints cover CPU intervals, empty writes, guard failures and
unsuccessful provider progress. Large fields retain per-chunk checkpoints.
Clock subtraction handles unsigned wraparound. No file-owned budget, allocation,
buffering, prefetch, asynchronous pending write, extra cache or new retry exists.

The normal `write` path uses a null budget and retains its original per-chunk
`delay(1)`. Page/line framing, images and footnotes retain ordinary writes. The
existing Section checkpoint every 16 completed pages remains. The non-volume
TextBlock caller uses its original ordinary serializers; the legacy HAL's additive
cooperative method delegates to its existing checked write path when explicitly
called by future code.

Preserved at each original write boundary:

- Request size/order and provider call, lock acquisition and mutation invalidation
- Writer/handle/media/pointer admission and sticky error behavior
- The original 16 MiB per-call cap, 4096-byte provider maximum, 20-second per-call
  deadline, with no extra retries or timeout inflation
- O_SYNC's per-chunk sync, short/zero response, provider error and return count
- All vector validation, page/footnote error propagation, Section callback result,
  page count, caller ownership and cleanup paths

Historical unchecked scalar/string writes still discard their results; this
scheduling change neither fixes nor weakens that preexisting correctness policy.
The serializer never closes or replaces its caller's file. A provider operation
already in progress remains subject to its provider's existing blocking contract.

## Deterministic evidence

The regression compiles complete production Page.cpp, TextBlock.cpp, Serialization
and actual page/block/style/footnote headers. It extracts unmodified complete
Section::onPageComplete and image serialize/deserialize methods and the actual
HAL write family. Storage/lock/media/clock/font endpoints are fixtures.

For 10 healthy 24-line / 240-word pages:

| Payload | Bytes | Provider writes / locks / mutation attempts | Old waits | New waits |
| --- | ---: | ---: | ---: | ---: |
| Ordinary | 35,800 | 13,700 | 13,700 | 980 |
| Focus | 43,000 | 18,500 | 18,500 | 1,220 |

All original writes and bytes remain; the residual small-I/O cost remains. Normal
section flags produce zero provider syncs. These are host scheduling/API counts,
not measured durations, physical SD sectors, device latency, current or FPS.

The original production source passes its functional regression and fails the
optimized-cost assertion. Normal, ASan/UBSan and non-volume caller controls compare
79 complete snapshot records against the original. Optional binary snapshots
contain every request position/size/result, complete output bytes and render-command
strings, allowing byte-for-byte comparison rather than only log fingerprints.
Healthy payloads also round-trip through the production decoder and reserializer;
render commands match the original page, including mixed image/footnote pages.

Cases cover 12/24/36 lines and 1/10/32 page callbacks; ordinary/focus styles,
UTF-8/empty/long words, underline rendering, mixed images/footnotes, O_SYNC, slow
providers, a 9000-byte string and the decoder's 10,000-word bound. Forty
short/zero/provider-error/sync-failure injections preserve payloads and calls;
subsequent fresh serialization succeeds. Mismatched vectors and checked footnote
failures preserve callback failure and page-count behavior. Close failure/retry
uses a storage fixture; full chapter cleanup/parser integration is unchanged and
is not claimed exercised by this focused callback test.

Paired ordinary/cooperative HAL tests compare guards, errors, short/zero response,
sync failure and the unchanged deadline. Null implementation, zero count and
byte/count/time/wraparound/CPU-interval checkpoints are additionally tested.
Corrupted legacy write output is deliberately not passed to the unchecked decoder.

## Reproduction

From the repository root:

```sh
python3 test/text_page_writes/text_page_writes_test.py --snapshot /tmp/current.snapshot
ASAN_OPTIONS=detect_leaks=0 python3 test/text_page_writes/text_page_writes_test.py --sanitize
python3 test/text_page_writes/text_page_writes_test.py --legacy

git show 36891e71ab651152c618d60d1d248abb4a7f44ff:lib/Epub/Epub/blocks/TextBlock.cpp > /tmp/original-TextBlock.cpp
git show 36891e71ab651152c618d60d1d248abb4a7f44ff:lib/hal/HalStorageVolume.cpp > /tmp/original-HalStorageVolume.cpp
python3 test/text_page_writes/text_page_writes_test.py --baseline \
  --text-source /tmp/original-TextBlock.cpp --hal-source /tmp/original-HalStorageVolume.cpp \
  --snapshot /tmp/original.snapshot
cmp /tmp/original.snapshot /tmp/current.snapshot
# Omit --baseline on the original inputs to verify optimized-cost failure.
```

The durable NativeApp host aggregate invokes both sanitized and legacy caller
controls. Local LeakSanitizer is unavailable under ptrace; ASan/UBSan remain active.
No physical raster/hardware, full chapter parsing/layout latency or device-speed
qualification is claimed. Target builds and exact-head CI status belong to the
published PR checkpoint, not the host operation counts.
