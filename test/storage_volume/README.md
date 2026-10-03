# Production provider filesystem regression

`python3 test/storage_volume/run_test.py [ArduinoJson/include/path]` compiles the actual SD driver, FatFs, and generic HAL against a GPIO-level in-memory SD model. The optional include path also compiles the production app manifest reader. It runs superfloppy/MBR media with rejected-write and stuck-busy failures. Linux uses ASan + UBSan; macOS uses UBSan because ASan initialization stalls on the task host.

This replaces the prototype sector test and sparse card fixture, whose fixed handle `1`, unsupported writes, and exact sector counts described the removed read-only implementation. Replacement coverage checks invalid BPB, self-linked FAT traversal, CRC failures, assigned pins, bounded busy timeout, no automatic formatting, and stale/retained ownership, alongside ordinary filesystem and shared-consumer behavior. Missing-card startup remains covered separately by `x4pro_sd_absent_test.c`.

The model is not hardware timing, power-loss, or physical card qualification. Failed writable handles intentionally remain owned until process exit; leak checking is disabled for that deliberate retained-generation scenario.

`bash test/storage_volume/run_gate_test.sh` exercises the actual X4 provider and
FatFs against the existing opaque OS/CPU ABI1 declarations. It covers allocation
failure before pins, two-thread and recursive contention, ISR/no-task refusal,
non-owner release, failed give poison retention, sleep prepare/cancel/commit
failures (including repeated terminal calls), safe restart, and deletion without
another fallible mutex operation after accepted quiesce. The wire-model suite
also injects failed gives after real FAT open/read/write/close/directory/metadata
operations. These checks intentionally retain poisoned mutexes until process exit.

The mutex switch is X4-only. `STORAGE_TRANSPORT=spi python3
test/storage_volume/run_test.py` checks the unchanged T5 SPI admission/transport.
The exact ABI1 + libc imports are checked with the production loader matcher by
`test/x4pro_import_match_test.py` after target builds.
