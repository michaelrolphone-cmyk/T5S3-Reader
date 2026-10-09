# EPUB anchor read cooperation

Run from the repository root:

```
python3 test/epub_anchor_reads/run_test.py --sanitize --board x4
python3 test/epub_anchor_reads/run_test.py --sanitize --board t5
python3 test/epub_anchor_reads/run_test.py --sanitize --negative-control
```

Requires a C++17 compiler and system Expat development library. Builds use `-g0`,
run sequentially in a temporary directory, and remove only their own outputs.

The default differential test compiles the complete production Section, chapter
parser, ParsedText layout, Page, TextBlock, CSS parser, entities and UTF-8 units,
plus the **complete production volume HAL**. It compares:

1. A reader-only control: disable only the operation-local anchor reader guard.
2. The unchanged checkout implementation, run twice for determinism.
3. The real non-volume call-site guard, with the volume HAL still linked.

`baseline.json` contains only provenance and SHA-256 hashes. If original Git
objects are available, `--verify-baseline-git` additionally verifies and executes
the original complete Section/HAL/Serialization files at commit
`36891e71ab651152c618d60d1d248abb4a7f44ff`. Alternatively,
`--original-source-root /path/to/source` requires the same exact files and hashes.
No frozen production implementations are checked into this regression.

## Workload and assertions

Valid XHTML with one internal final-note link and 16/128/512/1024 uniquely named
paragraphs passes through actual chapter parsing, layout, section/cache writing,
warm admission and first-page deserialization. Independently decode all produced
anchors and verify first/middle/last/missing/repeated lookup output. The largest
47,228-byte chapter produces 1024 anchors. Endpoint font metrics are six pixels
per ASCII byte and a 30-pixel line height; the resulting page counts are fixture
observations, not device-font claims.

Exact binary snapshots compare cache bytes, returned pages, provider request
paths/sizes/positions/bytes/errors, seeks, admission, opens, closes, lock counts,
retained handles and production storage-generation state. Only scheduling and
elapsed time are excluded. Last-anchor reads/bytes remain 3074/14342, while ideal
clock HAL waits change from 3074 to 96. Smaller cases assert 50 to 1, 386 to 12,
and 1538 to 48. The negative control must independently fail this bound.

Additional cases cover case-sensitive and UTF-8 keys; a deliberately duplicated
ID on different real layout pages, proving first-match behavior; positive short
provider chunks; slow reads and clock rollover; complete-copy errors, including
sticky errors; failed admission, defined failed seeks, invalid complete offsets;
cache replacement; empty maps; a cache-level 9000-byte key; ordinary-read negative
control; media recovery; and fresh retries. The real HAL destructor's failed close
retains its provider slot and invalidates generation reuse. A subsequent lookup
must not close that retained slot; final removal is explicitly fixture teardown.

Initialized-buffer HAL cases separately compare zero/partial/error reads,
deadline termination, oversize/null/zero-length requests, failed readiness/lock,
and no-progress elapsed checkpoints. Primitive checks exercise 32 reads,
4096 bytes, 8 ms, rollover, and checkpoint reset.

## Boundaries

- ZIP/EPUB extraction, fonts/rendering, mutex/scheduler and memory volume endpoints
  are fixtures. Unsupported image/hyphenation paths assert. System Expat is used;
  this does not execute the bundled firmware Expat or full EPUB ZIP admission.
- Complete actual HAL and generation/close logic run. This is not an SD/FatFs,
  physical card, device-latency, power or watchdog measurement. The established
  `test/storage_volume/run_test.py` suites provide separate X4/T5 transport coverage.
- The legacy variant executes the unchanged legacy **caller** selection. It does
  not pretend that the volume-backed fixture is a legacy SdFat integration test.
- Historical Section scalar reads ignore return counts and can leave automatic
  offset/count/length/page values indeterminate. Arbitrary truncated Section
  decoding is intentionally not executed or claimed safe. Complete-field
  copy-then-error and defined seek-rejection cases exercise the existing contract;
  incomplete I/O is tested separately on initialized buffers.
- ASan/UBSan run with leak detection disabled for traced-host compatibility.
