# EPUB long-token prefix metrics

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328)
- Report alias: `PERF-20261004-HYPHENATION-PREFIX-MEASUREMENT`
- Source and target: `xteink-x4-pro-boot`, exact base `9a209d8794d725393ddbb48d3880c03b9af29ba4`
- Repair branch: `perf/epub-prefix-metrics`; firmware 1.3.105 → 1.3.116
- [Report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979686111), [isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979833917)
- PR350/348 remain unmerged; this is not an implementation on master. Canonical report number remains coordinator-owned.

## Repair

`ParsedText::hyphenateWordAtIndex` previously rebuilt and decoded every legal prefix, including prefixes too wide to fit. Repeated long-token splitting repeated that traversal for successive suffixes.

An allocation-free operation-local table now measures ordinary prefixes and prefixes with an appended hyphen in one pass. The maximum token is 200 bytes and the table is 804 bytes. Sparse candidate sets keep the original path unless cumulative candidate-prefix bytes exceed four token lengths. All original candidate ordering, widest-fitting selection, splitting and continuation rules remain unchanged.

Built-in metrics preserve greedy/chained ligatures, including truncation and appended-hyphen substitutions. The last two glyphs stay pending so a changed final ligature recomputes its preceding kerning before the original differential rounding. Combining marks still stop ligature lookahead while contributing no ordinary advance. Soft hyphens are ignored exactly as in `measureWordWidth`.

SD fonts use only prepared nonzero RAM-table advances, summed in their original fixed-point units. A missing/zero advance, unavailable table, lazy glyph callback, malformed/NUL input, absent font or oversized token keeps the original measurement path. This preserves the later BUG239 on-demand SD-miss behavior on integration. No additional SD access, retry, persistent cache, font lifetime or cache-format change is introduced. The new loop is at most 200 RAM-only steps; it adds no potentially blocking operation.

## Verification

- Complete production ParsedText, Hyphenator and font implementations; verbatim renderer methods; shipped NotoSans 12 and SourceHanSansSC 14 fonts.
- 51,648 direct prefix comparisons, including synthetic fractional/negative kerning, ligature chains, hyphen/combining ligatures, absent glyphs, RTL codepoints and all eight style flags.
- 360 layout snapshots compare every output word, X position, style, focus boundary and suffix position against the original source. Normal and ASan/UBSan output is byte-identical; independent review reproduced the same digest. The digest is asserted by the default regression.
- Sparse words, invalid/oversized input, absent/lazy fonts, SD table not ready, missing advance, clear/reload and retry are covered.
- W200 plus trailing word at 400 pixels: 1,223 → 13 ordinary measurement calls; 87,962 → 2,547 instrumented renderer-loop decoded codepoints.
- CJK66 plus trailing word at 400 pixels: 202 → 7 calls; 4,950 → 405 instrumented renderer-loop decoded codepoints.
- Original-source negative control fails the deterministic cost assertion. Independent source and normal/sanitized review found no blocker.
- Cross-compiled all 11 test/production translation units with Xtensa ESP32-S3 GCC; includes complete ParsedText and font/hyphenation units, extracted renderer methods and fixture declarations. This is not a full renderer/firmware link.
- Full local Springboard and NativeApp aggregates passed; final exact-head hosted checks are recorded in the PR as they complete. Version, Python/shell syntax and whitespace checks pass.

Commands:

```sh
python3 test/epub_prefix_metrics_regression.py
PREFIX_SANITIZE=1 python3 test/epub_prefix_metrics_regression.py
python3 test/epub_prefix_metrics_regression.py --baseline-ref 9a209d8794d725393ddbb48d3880c03b9af29ba4 --enforce-cost
```

The third command must fail on the original cost assertion. Local sanitizer runs disable LeakSanitizer because it is unavailable under ptrace; ASan/UBSan remain active. Hosted CI keeps its normal sanitizer defaults.

## Limits and integration

Counts describe renderer-loop decode work, excluding EpdFont ligature-lookahead and other UTF-8 processing. They are not device timings or measured end-to-end speed/FPS. Storage/renderer construction are host fixtures; no device, visual capture, full EPUB parser flow or local full firmware link was performed. Direct embedded-NUL admission is checked, but full layout of embedded NUL is excluded because the pre-existing focus-token decoder stalls; this change does not repair that independent behavior.

The repair does not alter TXT paging/indexing, Grok PR384, other owner branches, the shared ledger or PR396/400/402/404. No merge, release, deployment, device or cross-repository action is part of this branch.
