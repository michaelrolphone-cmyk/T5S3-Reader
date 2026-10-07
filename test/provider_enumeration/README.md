# Provider enumeration cache regression

Run `python3 test/provider_enumeration/run_test.py` and repeat with `--sanitize`.
The storage-generation aggregate invokes the sanitizer run in hosted CI.
A local sandbox that prevents LeakSanitizer tracing can use
`ASAN_OPTIONS=detect_leaks=0`; this is not a leak-check pass and is not the
checked-in or hosted default.

The runner compiles the actual `nextProvider`, pathname/read/profile helpers,
and production `HalStorage.cpp` against the repository's existing fault-media
SdFat fixture. Only the already-prepared graph and manifest-inspection edge
are substituted. The test does not load hardware providers or replace
executable admission. The reused CDC observation test independently covers the
actual `prepare` guard and migration adapter.

Coverage includes zero-I/O unchanged positive/negative queries across capability
names/API versions; mutation/remount and writer/raw-access invalidation;
mid-scan mutation; read/open/directory/metadata/root-type errors and retry;
exact directory/candidate bounds; failed-close uncertainty; and overlapping
opaque cursors across successful and failed snapshot replacement.

To reproduce the negative control, pass `--source` pointing to the complete
original `InstalledProviderGraph.cpp` at
`cff6ef0c11b79634dfa2c688d880df393d8c153c`. It fails the first unchanged warm-query
assertion. Normal CI uses current source and needs no historical checkout.
