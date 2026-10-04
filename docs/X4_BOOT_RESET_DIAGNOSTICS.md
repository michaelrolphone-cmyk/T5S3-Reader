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
without a failure means the first destination presentation has not yet been
observed as successful. `requestUpdate(true)` queues the render task; it does
not wait synchronously. `ready` means that first physical presentation reported
completion. The heartbeat cannot emit a summary while its owner is still
inside a non-returning SDK call, and an early ROM/core failure can precede the
first diagnostic checkpoint.

## Follow-through after the diagnostic delivery

The following investigation used published source
`aa9b6075d9be03614090c7228d6384a223a306c2`, tree
`1202b77694372fb4d1762bc9c6f757868fccdb71`, firmware 1.3.122. Its firmware
ELF SHA-256 was
`148580112236bfea5d2702894b2eef788dc8a4bc8f0aefa80703c2a956b294e4`.
This follow-through changes tests and documentation only.

The production boot orchestration and loop were executed with injected mount,
package-validation, release and activation failures. In each case four modeled
minutes of disconnected loop iterations left `ready=0`. Switching the modeled
USB connection on for another ten seconds caused no additional mount,
validation, release or activation. Even a repeated bootstrap call returned
the cached failure. A successful first presentation remains the positive
control. These are software control-flow results, not a physical timing or
USB-power model.

The production `SdBootReader.cpp` was also executed against SDK/FatFs endpoint
doubles. Returned host/slot/card/filesystem failures unwind without publishing
the ordinary storage owner. A command requested with a long timeout is capped
at 250 ms; an expired operation refuses the next command. Uncertain file close,
controller deinitialization and rail parking retain bootstrap ownership and
prevent a second owner. This covers failures that return to Reader.

### The wrapper does not bound every SDK hardware poll

The linked firmware identifies its SDK as `v4.4.7-dirty`. The matching
[IDF host source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/sdmmc_host.c#L71)
busy-polls controller reset bits without a timeout. Its clock-update and
start-command paths also poll hardware without a deadline. The
[transaction layer](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/sdmmc_transaction.c#L108)
takes its request mutex with `portMAX_DELAY`; command timeouts are applied to
individual event waits. Reader checks its operation deadline before and after
these calls, so it cannot interrupt a call that never returns.

The actual delivered ELF confirms the reset loop at `sdmmc_host_reset`
`0x420d7458`: its branches at `0x420d7491`, `0x420d7499` and `0x420d74a1`
return directly to register reads at `0x420d748c`, without a timer call.
A source-extracted SDK register experiment returned when the modeled reset
bits cleared, but remained in the loop after the modeled 20-second Reader
deadline when a bit stayed set. The external host test timeout stopped that
negative experiment. This establishes a lower-layer boundedness limitation,
not evidence that the device hit it or that USB attachment would clear it.
The 250 ms/20 s/45 s limits must not be described as an end-to-end guarantee
against stuck SDK hardware polling. A safe future repair would have to
propagate checked SDK failure and preserve controller ownership; simply
returning while a worker still owns the bus would be unsafe.

`test/bootstrap_store/sdk_polling_probe.py` preserves that supplemental
experiment. It requires the caller to supply the pinned official
`sdmmc_host.c`, checks its SHA-256 before extracting the function, and never
downloads code itself. Run it with that file path as its sole argument. It is
not an automatic CI assertion that the SDK must keep this limitation; a future
SDK repair should replace the probe with a checked-timeout regression.

### Qualified reference differences

[Pinned FreeInk SD initialization](https://github.com/Free-Ink/freeink-sdk/blob/aef1a6c89e36f331b2e1aacbbf7ce0debdeb732a/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp)
configures the host/slot before the same 80 ms OFF / 120 ms ON cycle, sets the
slot's internal-pullup flag, validates a sector-0 read and allows up to four
complete read-only mount attempts. Reader cycles the gate before host/slot
configuration and admits one attempt. The actual pin assignments agree.
These differences are hypotheses for later discriminating evidence, not a
reason to change rails or introduce retries now. Espressif explicitly says
[internal pullups are insufficient](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/sdmmc_host.html#c.SDMMC_SLOT_FLAG_INTERNAL_PULLUP);
the flag is not proof that the board lacks its required external pullups.

The pinned Arduino 2.0.17 core performs CPU/PSRAM/NVS initialization before
calling setup. In the selected USB mode it does not wait for USB enumeration
there. The HWCDC USB bus-reset handler changes connection state and posts an
event; it does not restart Reader's setup. A host-driven hardware reset is a
separate possibility. The X4 Pro's GPIO0 side key is a boot strap, unlike its
GPIO3 power key, so holding that side key during reset can prevent normal ROM
boot; there is no evidence that this happened in the reported test.

The first retained `packages` failure currently distinguishes the stage but
uses a coarse reason for mount, package-validation and handoff refusals. A
live failed boot, a stalled lower-layer call, a new reset and physical power
assistance remain separate hypotheses. No one of them is established by the
old e-paper image alone.

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
