# CSS cache operation-local I/O cooperation

This focused host regression links the **complete production** `CssParser.cpp`
(including stylesheet parsing, cache serialization/deserialization and style
resolution) and **complete production** `HalStorageVolume.cpp`. It reuses the
small clock/mutex/heap fixtures from `test/epub_anchor_reads` and the headers from
`test/storage_volume`; it does not extract or replace the production I/O methods.

## Run

```sh
python3 test/epub_css_cache_io/run_test.py
python3 test/epub_css_cache_io/run_test.py --board t5
python3 test/epub_css_cache_io/run_test.py --negative-control
python3 test/epub_css_cache_io/run_test.py --sanitize --verify-baseline-git
python3 test/epub_css_cache_io/run_test.py --sanitize --board t5 --verify-baseline-git
```

Each ordinary invocation compiles three variants:

1. Current production with just the two CSS cache operation guards disabled.
2. Current production with the cooperative cache reader/writer enabled.
3. The legacy **caller** selection, with board macros undefined only for the CSS
   translation unit after including the volume-backed fixture's HAL header.

`--verify-baseline-git` additionally builds independently pinned original CSS and
HAL sources from commit `b91bc323fb736eb6a13ea1a9403470cf699c6128`. An exported
original tree can instead be supplied with `--original-source-root PATH`.
`baseline.json` records SHA-256s of all eight selected original source/header
files, and the runner pins that manifest's own hash. Both original-tree hashes
and the corresponding Git objects were verified. Only source selection and the
explicit two-guard control alter compiled production text; there is no extracted
function baseline or mock codec.

Each variant runs twice and must produce identical stdout and a byte-identical
snapshot. Across variants, snapshots compare complete provider request order,
paths, read/write request sizes and bytes, copied data, return values, final cache
bytes, all resolved enums/lengths/defined flags, rule counts, errors, generation
stamps, lock counts, and retained/closed handle state. Scheduler waits and
simulated elapsed time are intentionally excluded from semantic snapshots and
asserted separately. `--negative-control` independently requires the original
reader and original writer to fail their respective optimized scheduling bound.

## Realistic healthy workloads

Four production-parsed stylesheets have 16, 128, 512 and 1500 distinct ordinary
class selectors. Each specifies margin-left, font-weight and display. They are
1,039 / 8,315 / 33,263 / 97,451 bytes, all below the existing 128 KiB per-file
admission limit; the largest reaches the existing 1500-rule parser limit. No
rule-map injection is used. Every accepted rule is resolved before writing and
after each of three warm cache loads. A separate production-parsed stylesheet
covers every field/defined flag, all five length units, all parser-reachable
alignment values, and both font style/weight/decoration/display values.

| Rules | Cache bytes | Provider reads or writes | Original read waits | Cooperative read waits | Original whole-save waits | Cooperative whole-save waits |
| --- | --- | --- | --- | --- | --- | --- |
| 16 | 1,123 | 482 | 482 | 15 | 483 | 16 |
| 128 | 8,963 | 3,842 | 3,842 | 120 | 3,843 | 121 |
| 512 | 35,843 | 15,362 | 15,362 | 480 | 15,363 | 481 |
| 1500 | 105,003 | 45,002 | 45,002 | 1,406 | 45,003 | 1,407 |

The whole save includes one unchanged `mkdir(/cache)` wait in the complete HAL.
Subtract one for the scalar writer's waits. Provider I/O remains **30N+2** for
both read and write: this change removes scheduler amplification, not scalar
provider calls. The host clock has zero healthy transfer latency; additional
3 ms/9 ms transfer fixtures check the 8 ms cooperation interval and clock wrap.

## Focused fault and compatibility coverage

The final corpus has 664 recorded cases per variant, including:

- Full-width successful styles and mixed defined/undefined flags; three warm
  repeats; zero rules and an empty cache path
- Every scalar/header/selector I/O position through the first rule and into the
  next, with zero reads, copy-then-error and sticky errors, followed by fresh
  retries; short provider chunks of 1, 2 and 7 bytes
- Every truncation through the header and first complete rule, plus late-rule
  and final-field truncation; stale-version removal; excessive/zero rule count;
  zero, overlong and maximum permitted selector length
- Existing acceptance of trailing bytes, a lower declared count and unvalidated
  byte-valued enums, explicitly preserved rather than silently tightened
- Open/readiness/lock admission failures, missing cache, explicit media
  invalidation/remount, and media disappearance during both load and save
- Every scalar write position with zero, full-copy error and sticky error;
  short writes; retries; unchanged map/data generation accounting
- Initialized-buffer HAL cases for partial/error/zero I/O, the 16 MiB operation
  cap, the 20-second deadline, null and empty requests, unavailable media,
  failed locks, closed/read-only handles, `O_SYNC` success/failure, and failed
  explicit sync followed by a retry
- Pre/post elapsed checkpoints on zero/error transfers, 32-item and 4096-byte
  budget thresholds, the 8 ms interval, clock wrap and checkpoint reset
- Ordinary HAL callers retaining one wait per successful read/write
- Explicit close retry, failed reader/writer destruction retaining the provider
  slot and poisoning generation reuse, and subsequent CSS operations leaving
  that retained slot untouched

**Existing error behavior is not repaired by this scheduling change.** In
particular, `saveToCache()` ignores scalar write failures and destructor-close
failures, can publish truncated data, and still returns true after an admitted
failed write. The tests require the same success result, bytes, mutation history,
and handle state as the original. There is no new retry, buffering, validation,
cache format/version, maximum rule/selector size, or fault-handling promise.

## Boundaries

- Storage provider endpoints, mutex, scheduler clock, and ample reported heap are
  fixtures. Complete production generation tracking and HAL lifecycle execute.
- Normal CSS source acceptance executes; full EPUB ZIP/OPF admission,
  `ReaderActivity` dispatch, the renderer, physical SD/FatFs, UI latency and
  watchdog behavior do not execute here. The existing storage suites provide
  separate transport coverage. No hardware speedup or hardware validation is
  claimed from logical operation/wait counts.
- The legacy variant tests caller selection, not a complete legacy SdFat build.
- ASan/UBSan use leak detection disabled for traced-host compatibility. The final
  retained provider slot is explicitly removed by fixture teardown; that is not
  a claim that production destruction recovered it.
