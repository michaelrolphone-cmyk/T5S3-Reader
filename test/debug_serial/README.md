# Incremental debug-console regression

Finding: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261005-UI-SERIAL-LINE-STALL`,
[report](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6002643368).
Source and repair base: `xteink-x4-pro-boot`, PR350,
`b91bc323fb736eb6a13ea1a9403470cf699c6128`. Repair branch:
`perf/ui-serial-incremental-parser`. Firmware 1.3.143 -> 1.3.146;
1.3.144/145 remain reserved by PR430/431. No separate package payload changes.

## Change and contract

The existing diagnostic console recognizes only `CMD:` followed by trimmed
`SCREENSHOT`. `DebugSerialCommand` recognizes that language with fixed state
(at most 16 bytes), no heap allocation and no line-length cap. It retains case,
prefix positioning, Arduino whitespace and existing embedded-NUL comparison
semantics. Unknown commands are consumed through the next newline or timeout;
they cannot start a command midway through a line. The screenshot output block
and input/idle/activity/scheduler ordering in `main.cpp` are unchanged.

Each foreground poll processes at most 64 bytes or 2 ms of nonblocking HWCDC
reads and returns for existing input dispatch and loop scheduler cooperation.
It completes at most one line per poll, retains fragmented commands and the
configured Stream inter-byte timeout (normally 1000 ms). Pending lines expire
before a later queued retry is admitted. A work-limited poll records already
queued bytes, so the normal loop delay cannot split a buffered long command,
including with a zero/short custom timeout. Read failure retains state for the
next bounded poll; newline/timeout completion clears all parser state.

This removes foreground waiting, so timeout observation is necessarily at poll
granularity. HWCDC supplies no byte-arrival timestamps: bytes arriving before a
deadline but first observed after an exhausted-queue poll's deadline cannot be
distinguished from late arrivals. No exact arrival-time/scheduler-interleaving
parity is claimed. No timeout value or deliberate UI delay is shortened.
The unchanged screenshot output can still block; this repair only bounds input
collection. Running native apps/headless execution still bypass this block.

## Production evidence

`run_test.py` extracts and executes the final `main.cpp` dispatch block unchanged
and includes the real production parser header. The comparison executes the
unchanged source-baseline dispatch plus exact pinned SDK `timedRead`,
`readStringUntil`, `HWCDC::available/read` and relevant `String` methods.
`reference.json` records immutable source/fixture SHA-256 custody. Fixture refresh
is explicit: `build_reference.py <pinned SDK cores/esp32 directory>` requires
the baseline commit; ordinary tests need no network or SDK installation.

SDK package: `framework-arduinoespressif32 3.20017.241212+sha.dcc1105b`.
Upstream LGPL-2.1-or-later sources:
[Stream.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/Stream.cpp),
[WString.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/WString.cpp),
[HWCDC.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp).

Fixtures provide the queue, clock, memory-owning String shell and display/output
recorder. Clock advances on unsuccessful old reads; no wall-time measurement is
used. String allocation failure and its inherited all-whitespace pointer edge
are not established as safe production behaviors. The new parser allocates no
line and therefore does not reproduce allocation-exhaustion truncation.

Deterministic logical results, with zero modeled cost for available-byte reads:

| Input | Original dispatch gap | New largest collection gap | Original/new reads |
| --- | ---: | ---: | ---: |
| One unfinished byte | 1000 ms | 0 ms | 1001 / 1 |
| Fragmented command, 100 ms/byte | 1400 ms | 0 ms | 1415 / 15 |
| 20 unrelated bytes, 900 ms/byte | 18100 ms | 0 ms | 18120 / 20 |
| Complete buffered command | 0 ms | 0 ms | 15 / 15 |

The slow-20-byte case services input 1811 times over the modeled interval.
This is host operation/timing-model evidence, not measured device latency or a
zero-cost claim. A separate nonzero read-cost fixture verifies elapsed checks.

Coverage: ordinary/empty/unknown commands, all non-newline byte insertions at
every command position, whitespace/CRLF/NUL, every fragmentation point,
100 KB noise and 20 KB padded input, repeated commands and one-line ordering,
custom/zero/default timeouts, expired-fragment refusal/new-command retry,
32-bit timer rollover, read failure, unavailable queue, reconnect, state cleanup,
byte/time checkpoints and unchanged screenshot headers/payload/footer.
The original dispatch independently fails the optimized read-bound assertion.

Run:

```sh
python3 test/debug_serial/run_test.py
ASAN_OPTIONS=detect_leaks=0 python3 test/debug_serial/run_test.py --sanitize
ASAN_OPTIONS=detect_leaks=0 python3 test/debug_serial/run_test.py --sanitize --negative-control
```

The normal native host aggregate runs the sanitizer regression and original
negative control. `detect_leaks=0` is required only where executor tracing
prevents LeakSanitizer; it does not disable ASan or UBSan. No actual serial port,
screen pixels, physical timing, device test, release or flash is performed.
