# Vendor USB serial stream sessions

This source slice extends the optional Runtime 0.1.73 session contract to
CH34x, CP210x and FTDI. It is based on Reader
`134d56edec35dda20de016ff01bca4c756af4c4c` (CDC 0.1.9). The existing CDC
adapter is unchanged by this slice.

On 2026-10-08, live Reader master
`b5a9887e16ce04baecdfa8366ff7d67a2a22c3cc` and release-index
`19bb3dcb5cb763c4eacd884ad5fc0bcab57ff2ed` confirmed the following same-ID
versions in both source manifests and published records:

- `usb-ch34x-v2`: 0.1.7 -> 0.1.8
- `usb-cp210x-v2`: 0.1.8 -> 0.1.9
- `usb-ftdi`: 0.1.0 -> 0.1.1

## Behavior and ABI

Each driver exposes the tagged session extension after the complete
poll/stream/diagnostic/base prefix. CH34x and CP210x retain their existing serial
capability inventory/stream layouts. FTDI retains its original raw capability
layout; it does not acquire a discovery/inventory capability in this slice.
Legacy Reader open/configuration/I/O/cleanup remains available for raw sessions.
Tagged tokens cannot be adopted, configured, read, written or closed through
those raw callbacks.

The shared vendor adapter authenticates fresh tokens, copies request bytes,
checks a complete unique exact-generation host inventory, acquires one interface
claim, runs the actual vendor initialization and requested framing, and only
then publishes separate READ-only RX and WRITE-only TX queues. An absent,
truncated, wrongly tagged or incomplete deadline host suffix fails closed.
Every operation uses one 1..1000 ms total budget, with only its remainder passed
to each lower operation. Each bulk call also has a separately checked 1 ms bound.
No controller, PHY, VBUS, firmware bridge or package dependency is added.

CH34x retains its version-dependent baud divisor and framing restrictions.
CP210x enables its UART, writes baud and line settings, and acknowledges UART
disable before checked release while attached. FTDI fetches the device
classification through its claim, resets, purges RX/TX, disables flow control,
then sends data-format and chip-specific baud controls. Its modem teardown is
acknowledged before release while attached. Unknown inventory never authorizes
skipping a control; confirmed detach does.

The owner-task pump performs at most one read and one write per turn. Queue
chunks are at most 256 bytes. FTDI reads one complete USB packet (at most 512
bytes), removes the two status bytes, and retains any excess payload in its
existing bounded per-session buffer. It drains that buffer without another
physical read and rechecks generation before moving already-buffered bytes.

Cleanup stops pumping, acknowledges vendor teardown and physical release, then
closes queues. Partial publication uses the same ordering. Only successful
closes clear IDs. RETAINED fences locally before at most two terminal
`finish(RETAINED)` notices. Those notices immediately mark Runtime's queue
context unsafe; refusal preserves every surviving resource without retry.
No subsequent data, inventory, control or cleanup callback is allowed. Explicit
RETAINED is recognized before another class clock callback. Direct stop,
quiesce, stale tokens and later session calls cannot bypass the fence.

## Reproducible checks

Run these from this checkout. `RUNTIME` must be clean canonical Runtime
`b587df55298e0bb8e676b3d59ca13679c0267bf7`; the runner compares five contract
headers byte-for-byte. `HOST` is Reader host implementation
`d780fcbc93d5472eaa0dbc3f153b2f32f0c798e3` or an integration checkout containing
that implementation. It is a separate source input; this change does not edit
its host or lower-controller files.

```sh
bash test/run_usb_vendor_tagged_sessions_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_vendor_tagged_sessions_test.sh
bash test/run_usb_vendor_runtime_queues_test.sh "$RUNTIME"
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_vendor_runtime_queues_test.sh "$RUNTIME"
bash test/run_usb_vendor_host_runtime_test.sh "$HOST" "$RUNTIME"
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_vendor_host_runtime_test.sh "$HOST" "$RUNTIME"
bash test/run_usb_cdc_tagged_sessions_test.sh
python3 scripts/check_changed_package_versions.py --base 134d56edec35dda20de016ff01bca4c756af4c4c
NATIVE_DRIVER_CC="$CC_XTENSA" python3 scripts/build_usb_ch34x_v2.py
NATIVE_DRIVER_CC="$CC_XTENSA" python3 scripts/build_usb_cp210x_v2.py
NATIVE_DRIVER_CC="$CC_XTENSA" python3 scripts/build_usb_ftdi.py
```

The actual separately compiled class ELFs pass 123 deterministic tagged
scenarios plus all three existing legacy fixtures, normal and ASan/UBSan.
Cases cover literal vendor control order and arguments, every initial short
control failure, old CH34x chips, FTDI high-speed/FT-X divisors and status-only,
partial-status and full packets; exact and malformed inventory, copied open and
call requests, raw/stale isolation, single total deadlines and bulk sub-budgets,
partial RX/TX backpressure, unknown inventory pausing pending bytes, partial or
malformed publication, detach, explicit retained control/read/write,
physical/queue cleanup failures and refused terminal notification.

Nine tests use Runtime's production queues with each class: normal copied-byte
I/O and owner isolation, real second-publication capacity failure, and actual
registry-lock contention retaining allocations after physical release.
Eighteen additional tests load both the real class and the real deadline host,
using production queues and a simulated timed physical controller: normal I/O,
publication rollback, queue lock contention, detach/replacement, retained lower
I/O (including immediate unsafe Runtime context and blocked client bytes), and
total deadline exhaustion with retained physical custody.

All three Xtensa ET_DYN builds pass Runtime's real ELF structural validator.
Their only function export is `t5_driver_get`; their imports are `memcpy` and
`memset`. Shared pump preprocessing is unchanged when the new FTDI-only macro
is absent, and the unchanged CDC tagged/legacy suites pass.

## Limits

The shipping physical controller still does not advertise the lower timed
contract, so a complete hardware path remains intentionally unavailable.
No physical attach, unplug, power-loss, PHY/VBUS routing, board pin assignment,
electrical behavior, firmware image or full UI/broker stack is qualified here.
FTDI selection through a legacy inventory-based broker still needs a separate
capability-inventory extension or an already-known host device token.

The unmodified generic package staging command fails before selected package
staging because the base repository's unrelated `t5s3-sd` source graph lacks
`spi.bus@1` and `platform.clock@1`. This slice does not alter those dependencies
or claim a full repository staging pass. Target build and ELF validation are
separate successful checks. No public branch, release index, release, firmware,
product source or hardware was changed, and no artifacts were delivered.
