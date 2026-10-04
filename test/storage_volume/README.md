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

## Bootstrap power-hold handoff

The X4 runner compiles the exact production `SdBootReader::unmount()` into a
small host fixture. Its controller/GPIO endpoints are doubles; the production
cleanup must release the controller, park CLK/CMD/DAT and hold active-low GPIO5
HIGH before relinquishing ownership. The actual SD provider/FatFs/HAL then
starts from that state and must release the hold, mount, write and read.
The wire model now honors both the latched hold and card power. Previously it
ignored SD power writes and could return valid card responses while power was
held off, so a standalone successful inventory replay missed this handoff.

A negative build removes only the provider's initial hold-release call, matching
SD0.2.1's initialization transition. It must fail the same success assertion with
zero sector reads and an unavailable volume. This is explicitly a transition
negative control, not a second maintained copy of the old driver. The exact
historical0.2.1 provider was separately replayed during the investigation.
Unpowered CMD-low is modeled as an invalid CMD8 response; real floating voltage
may instead produce a timeout. This does not claim physical electrical proof.
T5 SPI behavior and all existing directory, write, sleep and mutex-failure
scenarios remain covered by their unchanged production paths.
