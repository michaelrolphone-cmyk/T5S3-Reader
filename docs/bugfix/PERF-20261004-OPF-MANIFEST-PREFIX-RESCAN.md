# Small OPF manifest cursor reuse

## Identity and scope

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (1367546328).
- Stable alias: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261004-OPF-MANIFEST-PREFIX-RESCAN`.
- Canonical ID: pending sole-coordinator reconciliation; no ID allocated here.
- Owner: `fix_reader_performance_1010`; branch `perf/opf-small-manifest-index`.
- Source and PR target: `xteink-x4-pro-boot`, captured baseline
  `9a209d8794d725393ddbb48d3880c03b9af29ba4` (PR350).
- Firmware: 1.3.105 to 1.3.112, above separate candidates through 1.3.111.
  No app, driver, provider, SDK ABI or cache-format version changes.
- [Queued reproduction](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978715747)
  and [isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5978929678).

At selection, PR350 and its PR348 base remain open and unmerged. Default master
`cff6ef0c11b79634dfa2c688d880df393d8c153c` is not the feature baseline. PR396's TOC
lookup and PR400's CSS ZIP lookup are distinct ready repairs. PR401 is unrelated
XTC metadata work. Grok PR384 and closed-unmerged consolidation inputs are
preserved. Fresh dedup checked all 401 PRs, 351 branches, 544 distinct heads,
current/historical report variants, issue searches and 57 PR332 comments. The
current Grok manifest/spine implementation was compared read-only and is unchanged.
The canonical ledger at `13f6c55cf482738f583efc58cd922e0afe835108` and its recorded
single writer are untouched; reconciliation is queued by comment.

## Reproduction and repair

`ContentOpfParser::startElement` already records each manifest item's hash, length
and temporary-file offset. Below 128 items it nevertheless starts every spine
lookup at zero. The full unmodified production parser with real Expat reproduces
32,512 reads / 243,840 bytes for 127 unique manifest/spine entries. Adding one
unused manifest item activates its existing large index: the identical ordered
127-entry spine then needs 508 reads / 3,810 bytes. All 32 workload/chunk/order
cases reproduce normally and under ASan/UBSan.

The repair retains the existing sequential decoder and full-ID comparison. For
eligible small files only, it starts that scan at the **first** insertion-order
hash-and-length candidate in the existing deque. Every exact ID must have that
key, so this cursor is at or before the first exact record. Hash collisions and
duplicate IDs therefore retain the first exact document-order match. Missing keys
start at zero. The existing >=128 sort and lookup are unchanged.

The hint adds no index, string buffer or heap allocation. Eligibility requires a
single manifest generation, at most 127 entries, each ID/href at most 4 KiB, and
a complete temporary file at most 128 KiB. Original serialization performs all
four writes; before/after handle positions, sticky errors, checked manifest close,
read reopen and final size guard hint use. A repeated manifest, write mismatch,
failed close/reopen, error or exceeded bound retains the old scan. These bounds
also make stored 16-bit lengths and 32-bit offsets lossless for eligible files.

The additional lookup is memory-only: at most 4096 hash bytes and 127 comparisons,
without blocking I/O. No new long-running scan, deadline or scheduling loop is
introduced. Existing storage/provider cooperation and error handling remain;
the original sequential scan does strictly less work on healthy unique matches.
Position/size/error checks inspect handle metadata; the current FAT provider uses
its in-memory file information, not a new pathname or disk scan.

A failed nonzero hinted seek gets one attempt to restore zero. A failed or sticky
retry stops Expat through its existing parse-error path before decoding from an
unknown position. A clean transient failure still uses the old scan. This guard
was added during independent review; no physical fault is claimed reproduced.

## Validation and limits

The focused regression compiles the complete production parser/class, real
Serialization and XmlParserUtils, and the production path normalizer with host
Expat. Storage and metadata-output endpoints are fixtures. It checks deterministic
reads plus exact ordered output, duplicate/collision behavior, eligibility bounds,
faults, retry and cleanup. `test/run_springboard_test.sh` runs it with ASan/UBSan.
The pre-existing guide fixture is updated only to match the actual bool seek/close
and size/error APIs; its original assertions remain.

Local PASS: 75 focused normal and ASan/UBSan cases; 55 original/repaired healthy
metadata/spine snapshots byte-identical; original source fails the new cost check
at 16 items (544 reads instead of 64); full Springboard and NativeApp aggregates;
package-source version guard, Python/shell syntax and whitespace; complete parser
translation-unit compile with the 32-bit Xtensa compiler and no exceptions/RTTI.
Independent review covered the source, tests and a separate strict/sanitized rerun.
No full local firmware link was run. Local LeakSanitizer is unavailable under
ptrace and is disabled only through caller environment; ASan/UBSan ran and hosted
defaults are unchanged.

Exact-head hosted results are recorded in the PR and terminal PR332 checkpoint.
At this committed checkpoint, final hosted CI is still pending; do not interpret
a focused pass as a complete target or hardware qualification.

No visual, feature, serialization, normal metadata, stylesheet, guide, TOC or
spine-order change is intended. Both cached normal opening and the existing OPF
caller/recovery flow remain unchanged. No cache-generation invalidation is needed.
The cost evidence is storage-API operation counts, not measured SD sectors,
device startup latency or FPS.

The inherited sequential decoder still uses unchecked legacy string reads, and
the inherited deque/strings still have their original allocation behavior. This
repair does not claim arbitrary-corruption equivalence, allocation-failure
recovery, full EPUB conformance, complete archive opening, real book-cache
serialization, physical SD/device/rendering tests, or correction of unrelated
large-manifest behavior. Skipped prefix reads necessarily change which hypothetical
physical faults would be encountered. Synchronous per-parser ownership is
source-traced; same-size concurrent external temporary-file replacement is not
detected by the hint's size check, nor by the existing large-manifest index.
No existing validation or recovery check is
removed. No merge, release, deployment, owner/default-branch write or device action.
