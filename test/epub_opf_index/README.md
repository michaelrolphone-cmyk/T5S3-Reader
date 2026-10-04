# Small OPF manifest cursor regression

Run from the repository root:

```sh
python test/epub_opf_index/run_test.py
CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  ASAN_OPTIONS='halt_on_error=1' UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
  python test/epub_opf_index/run_test.py
```

Requires Python 3, a C++17 compiler, and the system Expat development library
(`-lexpat`). The runner uses `TemporaryDirectory`, honors `CXX` and `CXXFLAGS`,
and leaves sanitizer settings unchanged. If the execution environment cannot run
LeakSanitizer, the caller can explicitly set `ASAN_OPTIONS=detect_leaks=0:...` and
report that limitation. The default does **not** disable leak detection.

## Production coverage and cost oracle

The runner compiles the complete unchanged `ContentOpfParser.cpp` and `.h` copied
from the requested source tree, real `Serialization.h` and `XmlParserUtils.h`,
and the verbatim `FsHelpers::normalisePath` implementation. It links real Expat.
Only storage/platform headers and the metadata-output endpoint are fixtures.
There is no replacement parser, callback extraction, or reimplemented lookup.

Normal mode exercises 75 complete-parser cases:

- 16/32/64/127-item manifests versus 128-item controls, forward/reverse references,
  and 1-byte/1024-byte XML input; exact ordered spine and metadata, four reads per
  directly resolved unique record, read/write bytes, seeks, unchanged writes.
- First duplicate selection, repeated references, genuine same-length FNV-1a
  collisions in both orders, missing and empty IDs, omitted/empty hrefs, empty
  manifests/spines, absent idref, null metadata sink, and normal path metadata.
- 4096/4097-byte fields, a 65536-byte ID, exact/over 128 KiB files, unchanged large
  manifests, and repeated manifest generations with misleading stale offsets.
- Partial final write, failed close/reopen, sticky write/read-open errors, size
  mismatch, transient optional seek failure, poisoned optional seek failures,
  parser abort, malformed XML, fresh retries, and repeated parser cleanup.

Every case checks balanced handles, at most one live temporary-file handle, and
removal of the temporary file. Partial-write bytes are compared with the original
native-endian serialization, including the unchanged declared length and missing
last payload byte. Optional-seek poison cases cover both a failed rewind and a
successful rewind with an outstanding error; neither may enter the decoder or
publish a stale href.

The baseline must fail the normal cost regression. Select only production parser
cpp/h from an exact Git revision without changing the checkout:

```sh
python test/epub_opf_index/run_test.py \
  --source-ref 9a209d8794d725393ddbb48d3880c03b9af29ba4
```

`OPF_TEST_SOURCE_REF` is an equivalent environment override. `--source-root` can
select a different complete source tree. The initial 16-item baseline case must
report 544 reads, rather than the repaired expectation of 64.

For complete healthy-output equivalence, use `--outputs-only` on baseline and
current tree and compare stdout byte-for-byte. This emits deterministic,
length-prefixed snapshots of all exposed metadata, CSS lists, and ordered spine
entries for 55 healthy cases. It deliberately disables I/O expectations and
omits fault-only cases; semantic output assertions and cleanup checks still run.
Compilation diagnostics go to stderr in this mode. A successful baseline
snapshot comparison is separate from passing the normal reduced-read suite.

## Limits and fault semantics

This is a parser/storage-API cost and behavior test, not a device latency, SD
sector, full EPUB archive, real book-cache serialization, firmware-build, or
hardware test. Fewer logical reads do not imply the same physical-I/O ratio.

Baseline `serialization::readString` ignores short-read results and can consume
an uninitialized length after a failed header read. The fixture aborts if that
undefined decoding path is reached; it does not invent safe legacy results for
arbitrary corrupt or truncated records. The partial-write case references an
intact earlier record and never decodes the damaged later tail. Unavailable-input
cases are bounded by fixture availability or the new optional-seek abort path.
Skipping a valid prefix intentionally changes which arbitrary storage faults
might be encountered; no universal corrupt-input equivalence is claimed.

No allocation-exhaustion recovery, physical backend timeout, or scheduler timing
claim is made. The production change adds only a bounded memory-only cursor-hint
lookup; existing sequential decoder and large-manifest behavior remain in scope
as output/cost controls, not as newly repaired corruption handling.
