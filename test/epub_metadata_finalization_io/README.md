# Cold EPUB metadata finalization scheduling regression

Run `test/run_epub_metadata_finalization_io_test.sh --board x4 --sanitize` and
repeat with `--board t5`. Add `--negative-control` for the original-cost negative
control. Without `--sanitize`, the same checks run normally. `--quick` selects
one ordinary EPUB plus all targeted fault cases for development iteration.
Dependencies: a C++17 compiler, C compiler, Python 3, and system Expat headers/library.

## Independent original and production execution

The default runner reads baseline commit
`b91bc323fb736eb6a13ea1a9403470cf699c6128` with `git show`, verifies the independently
pinned SHA-256 manifest, and builds its complete unchanged metadata, parser, ZIP,
inflater, tinflate and volume HAL translation units. An offline baseline tree can
be supplied with `--original-source-root`; every one of the 167 pinned files is
still checked. No original source is reconstructed by undoing the repair.

Four separate source trees/executables compare:

1. The complete unchanged original baseline.
2. Current production with only `buildBookBin`'s budget selection disabled.
3. Current production, unchanged.
4. Current production with its board guard disabled after selecting the real
   volume HAL header. This checks the legacy caller fallback, not the SdFat backend.

The negative control independently compiles the exact original source, turns on
only the optimized scheduling assertion in the fixture, and requires that
assertion to fail. It rejects unrelated crashes and compile failures.

The bounded memory volume, monotonic clock, scheduler, heap-availability and mutex
endpoints are fixtures. System Expat parses actual archive-extracted XML. The
`normalisePath` body is copied intact from the respective production source and
linked separately because the rest of FsHelpers depends on unrelated UI code.
The test reuses the existing anchor/storage host headers. No production parser,
metadata function, ZIP decoder or HAL transfer loop is substituted.

## Workload and invariants

Ten deterministic complete EPUB3 archives use 16 chapters with 4/16/64 sections,
128 chapters with one section, and 256 chapters with four sections, each stored
and deflated. They contain mimetype first/uncompressed, container.xml, OPF, nav,
and every referenced XHTML chapter. Python ZIP CRC validation is a fixture
integrity check, not a claim about production ZIP CRC checking. Both 257-byte and
1024-byte parser chunks run, giving 20 workloads per executable. The >=128-spine
workloads execute production batch chapter-size lookup and large-spine indexing.

Production OPF/nav parsing creates every scratch record. Finalization runs twice,
and an independent bounded decoder checks every host-produced metadata field,
LUT offset, cumulative chapter size, spine-to-TOC mapping, title, href, anchor,
level and spine index. All provider request sizes/order/data, results, errors,
positions, file/directory bytes, handles, storage generation stamps, mutex calls,
retries and cleanup are serialized and compared byte-for-byte across variants.
Every executable is repeated and must produce exactly the same snapshot and
stdout. The snapshots include original explicit per-record/ZIP yields. Only the
opted-in HAL delay count is omitted from equality; it is separately bounded.
Parser/scratch-creation scheduling is included in equality, so ordinary helper,
parser and archive callers cannot silently inherit the new budgets.

For healthy 16-spine workloads, original finalization delay requests are
`33 * TOC entries + 402`; the 1024-TOC workload retains 24,885 reads and 9,308
writes. The optimized scheduling bound requires at least an eightfold reduction
in metadata scheduling overhead after allowing unchanged ordinary ZIP reads.
This bound fails if either scalar metadata reads or writes revert to ordinary
per-transfer waits. Counts are logical provider calls and requested scheduler
waits, not physical SD requests or hardware elapsed-time measurements.

## Defined faults and boundaries

The first workload also exercises:

- Every output/scratch/archive open failure, missing archive, offline media,
  unavailable-generation state, failed mutex admission and healthy retries.
- Defined ZIP size-validation failures (21-byte input and a complete zero-filled
  22-byte input), preserving the original ignored size-lookup failure behavior.
- Empty cover/reference fields and a 5,000-byte metadata title crossing the HAL
  4,096-byte provider chunk boundary.
- Zero/short/error/sticky-error output writes and retry. Existing finalizer
  success/partial-publication behavior is intentionally preserved, not endorsed.
- Media disappearing after the final output transfer, with no subsequent scratch
  reads, followed by healthy retry.
- Each failed metadata close, retained member handle, another failed retry, then
  successful close/rebuild; explicit HAL close retry and destructor-retained slot.
- Elapsed-time checkpoints at 3/9 ms per transfer, including uint32 clock wrap.
- Direct defined HAL reads/writes covering chunks, short/zero/error results,
  per-call deadlines, invalid/null/oversized requests, admission, O_SYNC failure,
  flush/retry, readonly/closed handles and generation semantics.
- Read/write budget item (32 transfers), byte (4,096), time (8 ms), wraparound,
  repeated zero-progress checkpoint and reset behavior. Ordinary callers retain
  their one-wait-per-successful-transfer behavior.

There are deliberately **no failed scratch length reads**: the existing
serializer ignores read results and would use an uninitialized length. This
regression does not execute that undefined behavior as a successful/safe case.
It does not claim to repair BUG178's ignored write/close/publication failures.

## Scope limits

ASan is supported; UBSan is deliberately not claimed because canonical BUG256
owns the existing ZIP EOCD unaligned typed loads. LeakSanitizer is disabled in
this traced environment. This is not firmware, menu dispatch, complete
`Epub::load`, physical SD/FatFs timing, watchdog/power, or a device speedup test.

Host `size_t` is 8 bytes, firmware `size_t` is 4. The original producer writes
host-sized spine values, while production `load()` assumes a firmware-sized LUT
field. The independent decoder follows actual unchanged host output; production
warm reload is intentionally not executed and no firmware format round-trip is
claimed. No source patch hides that ABI mismatch.

## Recorded validation

X4 and T5, each normal and ASan: all 20 workloads, all four variants and each
repeat passed (640 executable workload runs). All four exact-original negative
controls failed only their intended optimized scheduling assertion. The first
workload has 101 deterministic normal/fault/retry/cleanup snapshots. Every full
snapshot matched across variants and board/sanitizer builds.

Representative stored/257-byte-chunk results (other compression/chunk choices
retain the same finalization counts):

| Spine | TOC | Reads | Writes | Original delays | Optimized delays | Explicit yields |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 64 | 1,845 | 668 | 2,514 | 283 | 14 |
| 16 | 256 | 6,453 | 2,396 | 8,850 | 482 | 50 |
| 16 | 1024 | 24,885 | 9,308 | 34,194 | 1,280 | 194 |
| 128 | 128 | 5,797 | 1,804 | 7,602 | 1,393 | 52 |
| 256 | 1024 | 29,989 | 10,508 | 40,498 | 3,544 | 248 |

The 16-spine/1024-TOC stored/257 snapshot SHA-256 is
`c5886781fd3e142aaf31e66a8539666ecacfa8acf857ac3a76a7d12d8eccb102`.
The 101-case stored/257 fault workload snapshot SHA-256 is
`f6d4baf31b69e09993d63f927032b4e57969492316e15d4da1baaa45a964057e`.
These are complete framed provider/state snapshots, not cache-file hashes.

Depth-one hosted checkouts fetch the exact pinned baseline commit from `origin`
only when absent. This is one depth-one, no-tags, no-ref-update fetch with a
120-second timeout; all 167 file hashes remain mandatory. Offline source-root
mode needs no fetch. A missing/unavailable baseline fails the test, never skips
its independent original. The local-only `test_baseline_fetch.py` exercises this
shallow-checkout acquisition and verifies that HEAD and branch refs stay intact.
