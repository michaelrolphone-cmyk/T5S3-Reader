# Production provider filesystem regression

`python3 test/storage_volume/run_test.py [ArduinoJson/include/path]` compiles the actual SD driver, FatFs, and generic HAL against a GPIO-level in-memory SD model. The optional include path also compiles the production app manifest reader. It runs superfloppy/MBR media with rejected-write and stuck-busy failures. Linux uses ASan + UBSan; macOS uses UBSan because ASan initialization stalls on the task host.

This replaces the prototype sector test and sparse card fixture, whose fixed handle `1`, unsupported writes, and exact sector counts described the removed read-only implementation. Replacement coverage checks invalid BPB, self-linked FAT traversal, CRC failures, assigned pins, bounded busy timeout, no automatic formatting, and stale/retained ownership, alongside ordinary filesystem and shared-consumer behavior. Missing-card startup remains covered separately by `x4pro_sd_absent_test.c`.

The model is not hardware timing, power-loss, or physical card qualification. Failed writable handles intentionally remain owned until process exit; leak checking is disabled for that deliberate retained-generation scenario.
