# Bookmark removal: skip unused chapter XPath resolution

Source-qualified report: [PERF-20261005-BOOKMARK-REMOVE-XPATH](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5990542267).
Baseline: `xteink-x4-pro-boot` / PR350, `36891e71ab651152c618d60d1d248abb4a7f44ff`.
Firmware: **1.3.128 → 1.3.134**, above separate PR419's 1.3.133 reservation.
No app, driver, provider, public ABI, cache format or bookmark format change.

## Change and preserved behavior

The menu and long-Confirm shortcut both call `EpubReaderActivity::addBookmark`.
It previously looked up the paragraph and resolved a KOReader XPath before
checking whether the current page was already bookmarked. Removal discarded the
result after streaming the chapter once (indexed) or twice (progress fallback).

Only construct the position and perform the same paragraph lookup and conversion
inside the existing addition branch. Removal still uses the original cached
identity and percentage-range test, erases every match, preserves other entries'
order, updates the same removal flag, creates the same directories and calls the
same persistence method. Addition retains its XPath/percentage, paragraph-page
selection, summary sanitization, insertion order and metadata fields. Guards,
page loading, fallbacks, persistence failure and subsequent toggle behavior remain.
Existing canonical BUG81's mutate-before-save semantics are intentionally unchanged.

No ZIP or parser implementation is changed. Existing complete decoding, bounds,
validation, errors, archive cleanup and scheduler cooperation apply whenever an
addition actually needs a chapter. No early decoder abort, new cache, allocation,
timeout or retry is introduced. The range calculation still reads existing cached
spine metadata; this is not a claim that removal performs zero total storage work.

## Focused regression and operation counts

`test/bookmark_toggle/toggle_test.py` extracts the complete production toggle,
range/matching and Epub stream/size/progress methods, and links the complete
production mapper, XPath resolver, ZIP, inflate/uzlib, UTF-8, BookmarkUtil and
shipped Expat. Two transparent wrappers count ZIP output calls/bytes. Section
lookup/page content, cached spine metadata, storage, clock, rendering lock and
bookmark persistence are fixtures. It does not model physical SD latency.

At this revision, **284 actions** pass in normal and AddressSanitizer builds.
The original source independently fails the new zero-chapter-work removal
assertion. Original, repaired and ASan state/persistence snapshots agree for all
284 actions. Ordinary additions retain their chapter/paragraph operation counts.
Injected short/error reads also retain bookmark output and cleanup; partial
inflater delivery counts after the existing read-error path are not asserted as
byte-stable. That path is unchanged by this repair.

Coverage includes stored/deflated 16 KiB, 128 KiB and 1 MiB chapters; indexed and
fallback addition; immediate/seeded/duplicate removal; percentage boundaries and
nonmatches; stable ordering; save failure and subsequent toggles; directory/page
load failure; archive open/header/metadata/seek/read failures and retry; invalid,
single and final page cases; zero paragraph/metadata; missing section/EPUB; and
malformed XML addition fallback/removal. Every opened archive is closed.

For a 1 MiB stored chapter:

| Removal path | Chapter bytes delivered before → after | Archive reads before → after | Archive opens before → after |
|---|---:|---:|---:|
| Cached paragraph index | 1,048,576 → 0 | 1,035 → 0 | 1 → 0 |
| Positive progress fallback | 2,097,152 → 0 | 2,070 → 0 | 2 → 0 |

These are host operation counts, not measured device time, responsiveness or FPS.
Local LeakSanitizer is disabled because it cannot run under the executor's ptrace;
AddressSanitizer stays enabled. No UBSan claim is made for the complete ZIP suite:
canonical BUG256 separately tracks the original EOCD unaligned typed loads.

Run normally with `python3 test/bookmark_toggle/toggle_test.py`, or set
`BOOKMARK_TOGGLE_SANITIZE=1` for ASan. The existing NativeApp aggregate runs ASan.
`BOOKMARK_TOGGLE_ACTIVITY` selects an original activity file;
`BOOKMARK_TOGGLE_BASELINE=1` relaxes only the new cost bound for output comparison;
`BOOKMARK_TOGGLE_RESULTS` exports all snapshots. Without baseline mode, original
source must fail the removal assertion.

Full firmware/aggregate and exact-head CI status belong in the PR's current
checkpoint. Local full firmware linking and physical device validation are not
claimed. The source-owner and default branches, shared ledger, releases and devices
are outside this repair's mutation scope.
