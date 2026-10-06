# Native text-view layout reuse

Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
Report: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261006-NATIVE-TEXT-VIEW-FULL-REFLOW`.
[Verified finding](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6008603467)
and [implementation claim / firmware reservation](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6009128669).
Canonical numbering stays with the existing ledger coordinator.

Original report source was PR350 `xteink-x4-pro-boot` at
`b91bc323fb736eb6a13ea1a9403470cf699c6128`. Implementation started from actual
PR433 integration `cbf44781cb36c12a716f32fcfbdb31ce95b40342` and is validated
with live PR350 `04d0e5daeebedb9ed759b82466b4f8820e17c365`, which includes PR434.
Firmware **1.3.154** is reserved after the 1.3.150–1.3.153 parity reservations.
The owner subsequently requested direct, single-writer integration into PR350
instead of leaving another repair PR. The source preparation branch remains
`perf/native-text-view-layout`; PR350 is the integration destination. No master
merge, release, deployment or hardware operation is included.

## Behavior and ownership

`NativeUiBridge::renderTextView` retains one bounded, compact snapshot of the
already-computed wrapped lines. An unchanged scroll compares the actual source
bytes and selects the visible lines without repeating paragraph wrapping,
text metrics or temporary string/vector construction. Pointer identity and
hashes are not used as substitutes for content equality. Geometry, scroll
clamping, hit layout, chrome and presentation are still computed each frame.

The key includes the renderer, effective user-content font ID, immutable regular
font-data identity, renderer font-registration generation, and wrapping width.
Every successful font insertion, removal and SD registration/clearing invalidates
that generation. Generation exhaustion permanently disables reuse rather than
wrapping to an old valid token. Any selected SD family bypasses cache lookup
before invoking its resolver, including failed loads that return a built-in
fallback ID; no storage-resolution or retry calls are added. SD and lazy-glyph fonts keep the
original preparation/measurement/retry path, because their metrics can change
with storage or glyph loading. Missing fonts also use the original path.

Content changes, including in-place mutation, append/drop and different buffers,
miss the snapshot and release it before rebuilding. Different widths or font
keys do the same. Height, inset and chrome changes with the same wrapping width
can reuse lines while recomputing current visible rows and positions. Normal
invocation entry/exit and successful UI API acquisition clear the snapshot;
list/table rendering clears it too. The existing retained-session quarantine
does not return or end its invocation and retains its existing resource policy.

Cold rendering keeps the complete existing wrapping algorithm, including its
newline/empty-line behavior, 96-line paragraph policy, truncation, total-lines
result and fallback for an empty wrapping result. Changed content still performs
the original full layout; this repair targets unchanged-layout reuse rather
than incremental append layout or the separate paragraph-cap correctness issue.

## Resource and failure bounds

Cache admission permits at most 16 KiB of source, 1,024 rendered lines and
48 KiB of total packed storage. These are cache limits, not limits on accepted
text: larger input follows the existing renderer. A single exact-size nothrow
allocation stores the source bytes, copied rendered strings and 16-bit line
offsets. Byte offsets are accessed with memcpy, without alignment or aliasing
assumptions. No pointers into app-owned text or unloaded app memory are retained.

Admission happens after the existing frame/chrome presentation. Allocation or
admission failure cannot replace the result already drawn. Copy work cooperates
after 64 items, 4 KiB or 8 elapsed milliseconds, with real `vTaskDelay(1)`;
a 100-ms deadline abandons only the optional snapshot. Partial storage is
discarded. Content comparison is bounded to 16 KiB plus its terminator.
The next render can retry admission. Font keys are checked again after drawing;
later registration changes cannot match the older saved generation.

For the reported 10,752-byte Serial Monitor history, retained packed storage
is **22,278 bytes**. Its first full render adds one optional allocation to the
existing cold path. This is bounded retained memory, not a claim that arbitrary
allocator exhaustion has identical timing to a renderer without a cache.

## Production and differential evidence

`test/native_text_layout_app/run_app_test.py` runs the complete unchanged
Serial Monitor app and its implementation include. The fixture delivers 192
ordinary 56-byte status paragraphs in 42 actual 256-byte app reads, then the
app handles Previous three times, Next three times, and Back. The production
text-view, layout, role-resolution, wrapping/truncation and renderer measurement
functions use the real Ubuntu10/EpdFont/EpdFontFamily/UTF-8 implementation.

At a 480 × 800 viewport with the ordinary status chrome, each unchanged scroll:

| Work | Original | Reused layout |
| --- | ---: | ---: |
| Paragraph wraps | 192 | 0 |
| Text-metric calls | 1,536 | 0 |
| Metric argument bytes | 40,128 | 0 |
| C++ allocation requests | 3,082 | 0 |
| Cumulative allocation-request bytes | 201,089 | 0 |

The first final-history render remains 192 wraps / 1,536 measurements, with
3,083 allocation requests and 223,367 requested bytes including cache admission.
Normal and ASan/UBSan counts agree. The six repeated scrolls therefore avoid
18,492 allocation requests and 1,206,534 requested bytes of allocation traffic.
These are host standard-library operation counts, not simultaneous heap usage,
physical latency, energy, raster timing or a device speedup measurement.

Every drawn line and coordinate, result field, hit-layout field, scroll position,
presentation and checked app release remain unchanged. The complete semantic
transcript matches the uncached source:
`dddbc471aceeeeac8b6aa8d227fe22a298c9acce4a3d1633800839215108bb0d`.
The original source fails the enforced zero-rebuild/zero-allocation warm bound.

An independently authored cache/render suite covers 500 random snapshots and
500 cold/warm render comparisons, exact content and each key field, mutable
buffers, geometry/chrome/scroll changes, missing/SD/lazy fonts, Unicode and
paragraph limits, maximum admission boundaries, failed allocation, cancellation
at every copy checkpoint, deadline/rollover, teardown, failure and fresh retry.
Its frozen pre-cache render oracle does not need Git history in CI. It uses
deterministic metric/render fixtures; the full-app suite supplies the real font
and application witness. Existing native-host launch/error/retry and UI-refresh
regressions remain enabled.

```sh
python3 test/native_text_layout/run_tests.py
ASAN_OPTIONS=detect_leaks=0 python3 test/native_text_layout/run_tests.py --sanitize
python3 test/native_text_layout_app/run_app_test.py --baseline-ref 04d0e5daeebedb9ed759b82466b4f8820e17c365 --output /tmp/native-text-baseline.records
python3 test/native_text_layout_app/run_app_test.py --enforce-cost --compare /tmp/native-text-baseline.records
ASAN_OPTIONS=detect_leaks=0 python3 test/native_text_layout_app/run_app_test.py --sanitize --enforce-cost --compare /tmp/native-text-baseline.records
```

Adding `--enforce-cost` to the baseline app command must fail. Both suites are
wired into the native-app aggregate, with fatal UBSan. Stream/device availability,
event delivery, presentation/controller, chrome and pixel output are fixtures.
Physical hardware and SD-font metrics/cache are unrun. Local LeakSanitizer is
unavailable under tracing; ASan and UBSan remain enabled. The allocation fixture
provides matching throwing/nothrow new/delete families without disabling the
sanitizer's allocation-mismatch checks.

## Related stable-head integration

The integration also preserves ready TXT-boundary PR435 at
`405b83736f879dc9c0e323cdb2f2ae665dab4db5` and the owner-requested parity heads:
PR419 `c03c30f4453ae93d17337d3ed055e85871b40792`, PR420
`e28b59aac9529fba4f1cd4639736c289abbdbc4b`, PR422
`6c03b68036b6d9b5997aa78c780ea06eb3c8d00a`, and PR423
`b77790854b24ec6d42f08b2e87ca79f2e6169c73`. Their commits and issue documentation
are retained. Default Reader stays 1.0.0; x4pro-buttons stays 0.1.5. Source
integration conflicts were confined to the common firmware version line.

The experimental USB native-host test step is aligned from 10 to 20 minutes,
matching the main workflow's existing native-suite limit; its overall job stays
30 minutes and no commands/assertions are removed. Three independently verified
parity jobs reached only the stale 10-minute limit while passing tests continued.
Superseded PRs are closed only after remote ancestry/content equivalence is
verified. Their prior CI does not qualify the final integrated head.
