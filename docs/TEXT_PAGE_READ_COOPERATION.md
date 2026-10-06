# Text-page read cooperation

Source-qualified report: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-TEXT-PAGE-DESERIALIZATION-READS`, [report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5992991285) and [claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5993379229).

Source/ref and repair base: `xteink-x4-pro-boot` at `36891e71ab651152c618d60d1d248abb4a7f44ff`; default master `21ce3b5b720e106815c8f3a7778bc7003294e0e2` does not contain the X4 line. Repair branch `perf/text-page-read-budget`. Firmware 1.3.128 → 1.3.139, above reservations through 1.3.138. No separately distributed app, driver, provider, format or SDK ABI changes.

## Narrow change

On the X4/T5 provider-volume backends, `TextBlock::deserialize` explicitly supplies one stack-local `HalReadBudget` to its existing field and string reads. Its budget has no file state or read-ahead. The original non-volume caller keeps ordinary serializers. The explicit overloads preserve each scalar/payload request and its ignored result exactly; this is not a decoder-validation rewrite.

The four HAL files reuse the exact helper/API implementation reviewed in [PR417](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/417) at `731d89083516bea55db0093fa4f6ff9e22fad40e`. This branch is independently based on the X4 source, does not include/change PR417's image caller, and leaves its branch alone. Identical shared HAL changes are intended to coalesce during owner integration.

A real scheduler wait is requested after 4,096 delivered bytes, 32 positive provider reads, or eight elapsed milliseconds, whichever happens first. Pre/post-read checkpoints cover time spent allocating/resizing between reads, including zero progress and errors. Existing per-read 16 MiB cap, 4,096-byte provider maximum and 20-second deadline remain. The deadline still cannot interrupt a synchronous provider that never returns. Page tags/coordinates and Section/image/footnote reads remain ordinary, including their waits between blocks. String allocation and malformed/truncated scalar behavior (existing BUG39/196) are unchanged, not claimed fixed. No global cooperation is removed and no timeout is increased.

## Deterministic results

Production Section page loading, Page/TextBlock, image serialization, Serialization and the verbatim HAL read implementation run against explicit storage/clock/font boundaries. Healthy ten-word/five-byte-string pages:

| Lines | Focus | Read/lock calls (unchanged) | Bytes (unchanged) | Scheduler requests before → after |
|---|---|---:|---:|---:|
| 12 | off | 688 | 1,800 | 688 → 52 |
| 12 | on | 928 | 2,160 | 928 → 64 |
| 24 | off | 1,372 | 3,588 | 1,372 → 100 |
| 24 | on | 1,852 | 4,308 | 1,852 → 124 |
| 36 | off | 2,056 | 5,376 | 2,056 → 148 |
| 36 | on | 2,776 | 6,456 | 2,776 → 184 |

These are logical host counts, not actual sleep durations, SD-sector reductions, hardware page-turn speed or FPS. Provider reads, lock acquisitions and bytes remain the same; their residual cost remains. The narrow caller avoids changing the whole page-loader signature or image partial-error semantics.

`test/text_page_reads/text_page_reads_test.py` checks exact request/position/byte traces, full reserialized payload and render-command equality. Real TextBlock rendering executes through a deterministic renderer boundary, including UTF-8, empty/long words, style/underline/focus variants and mixed image/footnote pages. These are command comparisons, not physical font raster/device validation. Image decoding/rendering is a fixture because that caller is unchanged.

The test also covers positive short reads, slow providers, count/byte/time/wraparound checkpoints, the 10,000-word bound, a 9,000-byte string, initialized error-after-copy responses, open failure, unknown tags, invalid footnote count, checked partial/zero footnote reads, close failure/retry, ordinary reloads, invalid HAL handles/media/locks/pointers and the unchanged read timeout. It deliberately does not execute historical unchecked scalar failures with indeterminate lengths.

Normal and ASan/UBSan runs must match the original TextBlock's output/I/O traces. The original fails the optimized cost assertion. The non-volume caller control retains the original scheduling counts even against the instrumented volume fixture; actual firmware targets are built separately by CI. Local LeakSanitizer is unavailable under ptrace; ASan/UBSan stay enabled. No merge, release, deployment or physical-device qualification is part of this repair.
