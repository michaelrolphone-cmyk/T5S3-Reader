# Installed provider admission regression

Run from the repository root, passing an existing ArduinoJson include directory:

```sh
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src --sanitize
```

Prerequisites: Python 3, a C11/C++17 compiler, OpenSSL development headers/library,
and ArduinoJson (the same dependency used by the existing storage-volume tests).
There are no downloads, platform-toolchain builds, external artifact requests, or
hardware requirements. `CC` and `CXX` select the compilers. Linux sanitizer mode
uses ASan/UBSan; Darwin uses UBSan. Leak detection is not disabled by the runner.
A sandbox needing `ASAN_OPTIONS=detect_leaks=0` must report that limitation rather
than describe its result as a leak-check pass.

## Production boundary

The runner compiles actual `GraphV2`, `ModuleV2`, `DeviceProviderExecutorV2`,
`BootstrapModuleStore`, the installed capability resolver, executable admission,
package SD adapter, `HalStorageVolume`, and the shared production FatFs volume.
It includes the complete production `InstalledProviderGraph.cpp` in the test
translation unit so it can call registration before executing an Xtensa module.
The only removed source line is its target-only 384-byte stack diagnostic;
64-bit host/sanitizer stack frames do not describe Xtensa target frames.
Production registration's embedded scheduler branches remain enabled.

The transport boundary uses the existing X4 MMIO/SD-card model. Both the T5 and
X4 drivers include the same production eight-slot FatFs directory implementation.
Logical calls, dependency behavior, pin ownership, and live cursor bounds are
asserted. Sector counts are informational and change with FAT layout. These are
not T5 SPI wall times, hardware activation tests, or an attribution of any user's
observed delay. Stream callbacks are inactive, and unsupported legacy paths
assert if reached. The graph, package validation, and executable admission are
not mocked.

## Authentic fixture

`fixtures/t5-expanded.tar.gz.b64` contains 19 unmodified delivered packages:
eight T5 boot packages, the ten-provider USB-navigation closure, and GT911.
The archive is deterministic gzip/USTAR stored as ASCII base64 for text-blob
publication. It is 521,260 compressed bytes / 704,161 encoded bytes, containing
1,223,093 original bytes in 86 files. The two original artifact SHA-256 values,
source commit, identities, versions, requirements, profile strings, and every
file's SHA-256 and length are in `fixtures/manifest.json`.

Before compiling, the runner verifies the archive hash, the complete allowlist,
all file hashes/lengths, and every package's declared entry digest. Extraction
rejects links, traversal, duplicate members, missing files, and extra files.
CI needs no network artifact access and never substitutes fake ELF payloads.
Fixture provenance is fixed to source commit
`1cb042921630b7d2f6613c790ee546cf4c26c560`; a fixture refresh must supply a newly
verified package set and update the manifest and archive together.

A separate capacity-boundary case creates clearly identified test-only package
IDs with regenerated, digest-correct metadata around one unchanged authentic
ELF. Those generated identities are not represented as delivered packages.

## Checks

- Navigation-first and touch-first admit all 19 actual packages. The deep
  navigation registration peaks at one live cursor regardless of dependency
  depth; every measured operation finishes with no live handles.
- Serial discovery accepts the 19-package inventory and ten unchanged repeats
  perform no reads. Forced warm navigation registration still scans one root and
  reads 19 profiles, with no ELF or package-manifest reread. This is intentionally
  distinct from the normal UI's retained successful lease.
- Topological admission independently crosses the original 16-module limit.
  The candidate fills its exact configured module/pin bound, reuses an existing
  provider when full, rejects one additional package, and preserves full reuse.
- Shutdown releases every ordinary/bootstrap/test-only package pin.
- Root-read and checked-close failures, profile/ELF short reads, and generation
  changes exercise cleanup, source revalidation, and successful retry.
- Match-list allocation refusals at the first, second, and fourth match use a
  size-targeted standard nothrow allocation replacement. They verify the concrete
  production allocation error, unchanged module/pin counts, cleanup, and retry.
- On-disk, same-length profile digest and ELF-header corruption are refused by
  registration. Restoring the original bytes allows admission again.
- A same-length ELF payload mutation preserving its valid header is deliberately
  retained by lazy registration, then refused by the real pre-mapping
  `admitInstalledExecutableSnapshot` integrity boundary. Restoring the original
  bytes and rebuilding the graph permits that same admission call. No ELF is
  mapped or executed in either path.

`--case navigation`, `--case touch-first`, `--case capacity`, and `--case faults`
run selected groups. The runner defaults to all groups.

## Original negative control

Use a separate complete checkout at the original commit; the runner never
modifies it or rewrites the fix into source:

```sh
python3 test/installed_provider_registration/run_test.py /path/to/ArduinoJson/src \
  --source-root /path/to/original-checkout --expect-original
```

This asserts the original failures: exactly eight live directory slots at the
failed open; ten repeated navigation attempts incur 100 root scans, 1,970 profile
reads, 2,260 total reads, and 7,430 explicit upper-layer waits. Touch-first retains
the same failure. The independent topological case stops at 16 modules, and a
full graph rejects reuse. Candidate-only injection checks are explicitly skipped
in this mode. Omitting `--expect-original` requires successful candidate behavior
and therefore rejects the original source.
