# EPUB image-cache cooperative read budget

Source: `xteink-x4-pro-boot` at `36891e71ab651152c618d60d1d248abb4a7f44ff`.
Report: [PR332 comment5988977608](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5988977608), stable alias `PERF-20261005-IMAGE-CACHE-SCANLINE-READS`.
Narrowed implementation claim: [comment5989271043](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5989271043).
Firmware **1.3.128 → 1.3.131**, coordinated above separate PR416/386 reservations1.3.129/1.3.130. No distributable app, driver, provider, cache format or SDK ABI changes.

## What changes

`ImageBlock::renderFromCache` keeps the original header reads, row allocation, row-sized requests, pixel writer and fallback path. A stack-local `HalReadBudget` amortizes scheduling between successful provider reads and checkpoints rendering after each row. Its only state is counters, a wrapping monotonic timestamp and clock/yield callbacks. The caller supplies `millis()` and **`vTaskDelay(1)`**, guaranteeing a scheduler tick rather than depending on millisecond-to-tick rounding. No buffer, global setting, retained file or retained generation is introduced.

The budget yields on **4,096 bytes, 32 successful provider reads, 32 rows or 8 elapsed milliseconds**, whichever threshold is observed first. Work between checkpoints is bounded by one existing provider call or one row; a threshold can be exceeded by that one bounded unit. Existing HAL count limits, provider chunk limit4,096, read deadline20seconds, lock/media/directory/null/error handling and short-positive-read assembly stay unchanged. No timeout grows, read size changes, read-ahead, retry, seek or skipped validation is introduced. Failing and empty operations receive an elapsed-time checkpoint on return. Operation-local counters are discarded after every render, including errors.

Ordinary `HalFile::read` enters the same implementation with a null budget and retains its existing per-provider-read `delay(1)`. Only this cache-render caller opts into the budget. The legacy SdFat adapter delegates to its unchanged read path and checkpoints the budget; its I/O/error-generation behavior stays unchanged. This is generic scheduling policy, not a new hardware/storage provider.

## Why bulk buffering was rejected

A local4KiB row-prefetch prototype changed the visible partial framebuffer after a hard provider error. With the first request crossing row17 failing, and original source unavailable, the unchanged480×800 renderer leaves8,160 black pixels but the prototype leaves0. The HAL does not expose bytes from the failing provider call. The page caller can present that framebuffer when decoder fallback fails, including its second image pass. Retrying/seeking merely to reconstruct old output would change retry semantics.

That prototype was discarded before publication. The delivered design retains the exact original provider request/error boundaries and preserves partial error frames. The existing two presentation passes are untouched.

## Production regression and measured scope

`test/epub_image_cache_reads/image_cache_reads_test.py` compiles the complete production ImageBlock, actual DirectPixelWriter, actual budget and unchanged extracted production HAL read methods. Media/provider, decoder output, clock, allocation and framebuffer surfaces are explicit host fixtures. An alternate original ImageBlock can be supplied with `--source`; `--baseline` asserts original cost. Without that flag, original source fails the optimized-wait bound.

Coverage includes all four orientations, all three grayscale planes, odd widths, tiny images, repeated two-pass rendering, short positive provider reads, truncated header/payload, dimension rejection, hard error at a partial-frame boundary, missing source/cache, failed/successful decoder fallback, allocation failure, lock/media refusal, media loss after17rows, slow providers and slow short reads hitting the unchanged20-second per-call deadline, exact provider request/return sequences, every framebuffer byte and handle/allocation cleanup. Separate budget assertions cover byte/read/row/time thresholds, clock rollover, slow CPU work and fresh operation state. Existing image identity/settings/tolerance/failure/retry tests remain enabled.

Healthy480×800, two passes:

- **Provider requests:**1,604 before and after, byte-identical sequence
- **Returned payload:**192,008bytes before and after
- **Original waits:**1,604 HAL `delay(1)` calls plus50 row `vTaskDelay(1)` calls
- **New waits:**4 ordinary header `delay(1)` calls plus50 budget `vTaskDelay(1)` calls
- **Framebuffer and cleanup:**exact equality
- **Pixel scratch:**same120-byte row allocation; no full-image or4KiB buffer

This removes1,600 small-read HAL wait requests in the fixture. It does **not** reduce provider or physical I/O, prove actual scheduler sleep durations, measure MCU/SD/display latency, or establish a hardware speedup. Clock callback tests show checkpoint behavior, not real-time preemption guarantees for a blocked provider call. Production provider timeouts remain responsible for that boundary.

All220 normal and220 ASan/UBSan snapshots pass and match original output/request snapshots. The slow-short-read controls inject5seconds per at-most7-byte provider result: both implementations stop after four such reads at the unchanged20-second boundary, with identical partial frames and fallback. One fails before any complete row (6requests/32bytes including header); the other follows17 complete rows (312requests/2,072bytes). No provider error is injected in those deadline cases.

Final local full NativeApp and Springboard aggregates, complete production storage/FatFs regression, package-source version guard, Python/shell syntax and whitespace checks pass. Independent parent source review and focused rerun found no blocker. Local LeakSanitizer is disabled only because the executor's ptrace environment prevents it. Hosted builds and exact-head CI are tracked in the PR. Physical device qualification is not part of this repair; no merge, release or device action is authorized here.
