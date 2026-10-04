# X4 battery-only reset investigation

## Observed state on 1.3.120

On 2026-10-04 the owner confirmed battery and time availability and then
repeated desk-clock minute updates. Those are physical passes for those
functions. They do not establish power-button return, current draw or reliable
battery-only reset/boot.

A separate owner test left the old e-paper image visible for four minutes
after reset on battery; attaching the computer was followed by a visible boot.
Without a timestamped startup log this does not distinguish a live stalled or
failed boot, a new reset, USB host assistance or power assistance. E-paper
retains its image without code executing.

## Source investigation

The delivered X4 setup already asserts and holds GPIO1 before serial setup,
SD or providers. Its checked output-before-unhold sequence matches the pinned
power reference documented in [clock recovery](X4_CLOCK_POWER_RECOVERY.md).
The actual helper's cold/deep-reset/hold/failure tests pass. This is source
coverage, not an electrical measurement.

The explicit serial-ready loop is bounded to 500 ms. The selected Arduino
2.0.17 [HWCDC implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)
has no host wait in `begin()`; its disconnected write path uses a bounded
buffer/drop policy. The SDK's
[USB console](https://github.com/espressif/esp-idf/blob/v4.4.7/components/vfs/vfs_usb_serial_jtag.c)
uses a 50 ms fail-fast policy after its FIFO stops draining. The delivered
firmware ELF contains this timeout-based console implementation. These findings
do not support attributing four minutes to the explicit serial-ready loop.

[Pinned CrossPoint setup](https://github.com/crosspoint-reader/crosspoint-reader/blob/a68548d9c43793b90cff8e7bf8a8da735c1535e1/src/main.cpp#L430)
sets a 1 ms HWCDC TX timeout, while Reader leaves its 100 ms default. That is
a concrete difference, but no reproduction connects it to this battery-only
report, so this diagnostic change does not alter the transport.

Several existing X4 setup failures return to a loop that emits `heartbeat
ready=0` every two seconds. Those messages overwrite the 16-line RTC log ring
within about 32 seconds. Thus the original error can be gone when USB is
attached after four minutes. No automatic retry currently follows these
returns. This is the reproduced diagnostic gap addressed here; it is not a
claim that a specific provider caused the physical failure.

## Diagnostic behavior

`X4BootDiagnostics` records boot-stage entry time, reset reason and the first
explicit failure in a fixed 140-byte RTC-NOINIT record. It runs only on the
existing setup/loop owner, immediately after the unchanged first board-alive
call. It adds no SD writes, peripheral reads, retries,
provider acquisitions, sleep modes or pin/rail changes. Existing startup and
failure-return behavior remains the same.

The existing heartbeat checks the serial connection and emits a bounded
summary once when a connected edge is observed. The current record survives
arbitrarily many offline heartbeats. A valid previous record is reported
separately after software, panic, watchdog or deep-sleep reset. Cold power-on,
external/EN, brownout, unknown and other reset types never establish retained
RTC continuity and discard prior bytes. A schema, bounded enum/string check
and checksum reject corrupted or partially updated records. There is no
promise that the record survives battery removal, brownout or hardware reset.

A stage names the last operation entered, not proof of its completion or a
hardware root cause. A failure reason is copied when an existing return path
rejects setup. Optional RTC/battery absence retains its ordinary diagnostics;
it does not become a new boot admission rule. A record at `home-present`
without a failure can identify work still inside the existing first-render
wait. `ready` means that first physical presentation reported completion.

## Checks and remaining evidence

The production diagnostic fixture covers four minutes offline, repeated and
reconnected polling, first-failure copying and preservation, all accepted and
rejected reset classes, corrupt/torn/schema-invalid records, maximum reason
length, every stage and 32-bit timestamp rollover. ASan and UBSan are enabled;
LeakSanitizer is disabled in this ptrace executor. Boot-isolation checks verify
that checkpoints precede existing work and connection observation cannot
restart initialization. The shared desk-clock aggregate remains applicable.

The next useful physical log is the first `X4BOOT` summary after attaching the
computer to a battery-only failed boot, together with the `Boot firmware`
reset-reason line if a new boot occurs. This distinguishes continued uptime
from a fresh reset without inferring code progress from a retained image.
The diagnostic increment does not itself fix or qualify battery-only boot.
