# Exact bounded title-truncation prefixes

## Identity and scope

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
- Alias: `PERF-20261005-TITLE-TRUNCATION-PREFIX-REMEASUREMENT`; canonical ID awaits the sole ledger coordinator.
- [Original report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5986738024).
- [Implementation claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5989956167).
- Source/base: `xteink-x4-pro-boot` at `36891e71ab651152c618d60d1d248abb4a7f44ff`; independent branch `perf/title-truncation-prefix`.
- PR350 is unmerged and now targets master directly; PR348 is closed/unmerged. Actual master was checked at `21ce3b5b720e106815c8f3a7778bc7003294e0e2`.
- Firmware 1.3.128 → 1.3.132, above separate 1.3.129–1.3.131 reservations. No app/driver/provider package, SDK ABI, font or cache format changes.

## Production change

Long chapter labels reach the shared BaseTheme truncator on every list redraw. The old algorithm measures the whole label, then repeatedly constructs and measures each shorter UTF-8 prefix with an ellipsis. For an 8,192-character synthetic title, the shipped Ubuntu10 font measures 33,590,896 input bytes across 8,168 full-width calls.

The new optional `GfxRenderer::getTruncationPrefix` retains the existing font lookup, style selection, glyph fallback and fixed-point kerning APIs. It carries only the horizontal state required by `EpdFont::getTextBounds`, examining every accepted UTF-8 prefix plus the ellipsis and remembering the longest strict fit. It never assumes monotonic widths and never substitutes advance widths for built-in bounding boxes. Negative bearings, negative kerning, zero advance, absent glyphs and missing ellipsis remain meaningful inputs.

Both `BaseTheme::truncatedPreparedText` and `GfxRenderer::truncatedText` use this same helper only after their original full-string fit check. Initial fit remains `<=`; ellipsis candidates remain strictly `<`. No fit still returns the ellipsis alone. The existing input string is resized once; the helper allocates no table, buffer or persistent cache.

Admission is deliberately narrow: at most 8,192 input bytes; static font metrics with no lazy glyph callback; no SD-font registration; valid supported UTF-8 with no combining mark or replacement-codepoint path; and no actual adjacent-input or prefix/ellipsis pair that would ligate. A font can contain a ligature table without the current text using it. Any unsupported case retains the complete original algorithm and its existing font preparation/I/O behavior. Soft hyphens retain their original glyph semantics rather than adopting the separate EPUB word-layout filtering policy.

Work is capped by input bytes and an optional 1-second helper budget. Each 256 bytes or 8 elapsed milliseconds requests one real FreeRTOS tick via `vTaskDelay(1)`. Unsigned elapsed arithmetic handles clock rollover. A refusal or optional-budget expiry leaves the output parameter untouched and falls back to the original algorithm. This is **not a new user-visible timeout** and does not discard, truncate or reject the input.

## Evidence

The regression extracts the complete current production helper, both production truncators, role preparation/measurement and the renderer width API. It links unchanged `EpdFont.cpp`, `EpdFontFamily.cpp`, `Utf8.cpp` and shipped Ubuntu/Noto font data. Its independent reference implements the original descending-prefix policy, and every width uses the real production font API.

Coverage includes negative bearings/kerning and independently observed nonmonotonic candidate widths, missing/replacement glyphs, initial/strict-fit boundaries, all style/underline combinations, Unicode/soft hyphens, actual input and suffix ligatures, combining and malformed text, unloaded font, empty/null/nonpositive input, 8,192/8,193-byte bounds, unchanged lazy/SD fallback calls, failure/retry, scheduler byte/time checkpoints, rollover and optional-budget expiry. Display and SD-readiness endpoints are fixtures, not device or physical SD tests.

For the original synthetic 8,192-character Ubuntu title at 420 pixels, output remains 26 `A` characters plus U+2026. Full width calls decrease from 8,168 to one; that one call measures 8,192 input bytes, followed by 8,192 constant-state prefix steps and 32 scheduler-yield requests. Measurements for 128–8,192 bytes remain linear. These are host operation counts, not measured device time, frame rate, allocator totals or physical throughput.

Reproduce:

- `python3 test/title_truncation/truncation_test.py --enforce-cost`
- `python3 test/title_truncation/truncation_test.py --sanitize --enforce-cost`
- `python3 test/title_truncation/truncation_test.py --baseline-ref 36891e71ab651152c618d60d1d248abb4a7f44ff --enforce-cost` must fail the new cost bound.
- `CXX=<xtensa-g++> python3 test/title_truncation/truncation_test.py --compile-only` checks 32-bit compilation without claiming target execution.

The existing native-app aggregate runs the sanitizer/cost regression. Local LeakSanitizer may require `ASAN_OPTIONS=detect_leaks=0` in the executor's ptrace environment; the committed runner retains hosted defaults. Final PR/remote SHA/check results are recorded in the PR332 terminal checkpoint rather than implied by this document. No merge, release, deployment, hardware action or Watch change is part of this repair.
