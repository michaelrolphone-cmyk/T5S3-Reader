# TXT/Markdown paragraph boundary reuse

Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
Stable report: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-TXT-WRAP-SUFFIX-REMEASUREMENT`.
[Original finding](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5998502138).
[Isolated repair claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6008581278).
Canonical numbering remains with the existing ledger coordinator.

Source/ref/base: `xteink-x4-pro-boot` at
`b91bc323fb736eb6a13ea1a9403470cf699c6128`, [PR #350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350).
Repair branch: `perf/txt-paragraph-boundaries`.
Default master is `21ce3b5b720e106815c8f3a7778bc7003294e0e2`;
PR #350 remains unmerged. PR #348 is closed-unmerged, with its head included
in PR #350 rather than master. Firmware **1.3.143 → 1.3.149**, above the
1.3.148 reservation. No independently distributed package is changed.

## Focused change

`TxtReaderActivity::loadPageAtOffset` previously constructed a new UTF-8
boundary vector for every overflowing suffix of a parsed source line. It now
constructs that same vector lazily once and uses its remaining suffix for
subsequent wrapped lines. `upper_bound(consumed)` preserves the exact original
boundary sequence even after a removed space followed by malformed continuation
bytes. The original binary search still receives the same number of remaining
boundaries and measures precisely the same substrings in the same order.

This repairs the repeated boundary scan/allocation portion of the existing
report. **All existing font measurements remain.** It does not remove the
report's larger remaining remeasurement cost or change word wrapping. Output,
font/kerning/ligature behavior, Markdown transformation, page offsets/fences,
storage calls, validation and existing per-16-wrap yields remain unchanged.
No unmerged PR #434 metric API or PR #433 storage primitive is copied.

The existing vector is retained only within one parsed line in one page call;
it is never a persistent text/font/storage cache. Short fitting lines allocate
no boundary table. Work and storage remain bounded by the existing 8 KiB read
chunk plus Markdown decoration (a quote can produce 8,195 display bytes and
8,193 boundaries). No new input limit, error, retry or timeout is introduced.
The retained vector changes live-allocation timing relative to the baseline;
identical behavior under total allocator exhaustion is not claimed. Its
maximum table is the same table the original first overflow already builds.

## Deterministic production regression

`test/txt_paging_test.py` compiles the complete production page method and
boundary helper, unchanged Markdown/UTF-8/EpdFont/EpdFontFamily/SdCardFont
implementations, actual renderer measurement/line-height methods and real
Noto Sans 12 regular/bold fonts. File contents, malloc/read fault injection,
SD-font preparation, scheduling and display ownership are fixture boundaries.
The SD preparation callback is exercised with a fixture. The SD metrics/cache
path and physical SD font I/O are not exercised; forwarded metric requests
remain identical and built-in metrics provide the actual measured widths.

All **947** case transcripts and two complete 64 KiB document walks match the
unmodified baseline exactly, including every measurement argument and style,
line/heading result, offset/fence state, read and buffer-cleanup event, and
existing scheduler-yield count. Transcript SHA-256:
`cdef11cc580573cd1f0dfb78ced57d520d790321317228f3f8813cd65b5c44f7`.

Normal and ASan/UBSan runs pass. Applying the cost assertion to the original
production code fails on the first 1 KiB paragraph, as intended. Independent
review also checked 757,305 suffix-boundary cases and 50,000 randomized wrapping
comparisons, including call-dependent/nonmonotonic widths, without a semantic
counterexample. That review is additional host evidence, not a target build.
Review corrections add explicit unfenced heading and expanding-quote
witnesses, assert actual bold measurement and the 8,193-boundary expansion,
record ordered buffer allocation/free and yield events, and make UBSan
nonrecoverable in the checked-in sanitizer command. Cost assertions also
enforce one table per overflowing paragraph and none for a fitting line.

For an ordinary prose paragraph and the real 400-pixel viewport:

| Paragraph bytes | Boundary visits, before → after | Tables, before → after | Vector growths after reserve, before → after | Width calls, unchanged |
| --- | --- | --- | --- | --- |
| 1,024 | 14,211 → 1,024 | 18 → 1 | 18 → 1 | 188 |
| 4,096 | 69,507 → 4,096 | 18 → 1 | 54 → 3 | 234 |
| 8,192 | 143,235 → 8,192 | 18 → 1 | 72 → 4 | 250 |

Each complete 64 KiB TXT/Markdown walk has 129 pages; boundary visits fall
from 17,278,452 to 991,136. At 540 pixels, the 8 KiB page falls from 141,097
to 8,192 visits while keeping all 251 width calls. These are host operation
counts for the checked-in fixture, not hardware latency measurements.

Coverage includes repeat calls, independent pages, changed content, long
paragraphs, 8 KiB chunk boundaries and expanding Markdown quotes/lists,
headings/fences/CRLF, embedded NULs, combining characters, Unicode and malformed
byte mixtures, missing fonts, narrow/zero/negative widths, page budgets,
allocation failure/retry, read failure/retry, EOF/past-EOF and checked cleanup.
Existing short-read and Markdown source-offset policies are not redesigned.

Reproduce from the repository root:

```sh
python3 test/txt_paging_test.py --baseline-ref b91bc323fb736eb6a13ea1a9403470cf699c6128 --output /tmp/txt-baseline.records
python3 test/txt_paging_test.py --enforce-cost --compare /tmp/txt-baseline.records
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 TXT_SANITIZE=1 python3 test/txt_paging_test.py --enforce-cost --compare /tmp/txt-baseline.records
```

The baseline command with `--enforce-cost` must fail. The sanitizer regression
is wired into the existing Springboard host aggregate. Physical hardware is
unavailable/unrun; local LeakSanitizer is unavailable under tracing, while
AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## Coordination

Pre-claim dedup checked 434 all-state PRs, 384 live branches, 577 distinct heads,
194 preserved report-document blobs, zero non-PR issues, the canonical ledgers
and all 169 PR #332 comments. Both paging variants among the 575 source-bearing
heads have identical wrapping bodies. PR #433 and its closed source PR #416
alter separate content-read plumbing and retain the affected boundary loop.
PR #433/#434 are terminal ready with released active claims, awaiting owner
integration; neither is modified here. Shared ledger ownership stays unchanged.
No master/owner-branch update, merge, auto-merge, release, deployment, flash,
device or schedule action is included.
