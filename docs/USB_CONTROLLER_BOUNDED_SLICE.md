# Private bounded USB controller slice

`usb-controller-esp32s3` 0.1.21 adds a reusable controller-owned bulk request
state machine and exact ESP-IDF v4.4.7 event/cancellation adaptations. It does
**not** advertise `risc_usb_controller_deadlines_v1`, activate this path from a
public callback, or qualify the physical X4 Serial Monitor backend. The complete
deployed interrupt/diagnostic prefix and normal legacy behavior remain intact.
The completed class/host deadline adapters therefore still reject this physical
controller for tagged sessions.

## Implemented contract

`OwnedBulkRequest.h` uses the existing controller allocation: one request and a
maximum 4096-byte payload. It copies outgoing bytes before returning and never
stores an incoming destination. `take()` validates the owning claim and copies
completed input into the destination supplied on that call. Admission validates
the live endpoint's client, interface number, alternate setting, type and MPS.
Only one URB is admitted on that endpoint. These are private controller entry
points in `driver_base.cpp`, with no new public capability or Runtime API.

The serialized owner calls `step()` once per cooperative poll. A step makes at
most three nonblocking SDK calls and dispatches at most one controller callback;
there is no worker task, sleep, synchronous drain, retry loop or role/power
operation inside it. The existing owner must return to its scheduler between
polls. State, claim, deadline and callback-custody observations are directly
available to that owner; the slice does not add a per-byte logging stream.

One absolute 1..1000 ms deadline covers admission, submission, completion,
cancellation and endpoint restoration. The final `min(25, budget/4)` ms is
reserved for cancellation rather than starting a second drain timeout. The
private clock accumulates wrap-safe RTOS tick differences, is never reset on
controller restart, and requires owner sampling at least once per tick-counter
wrap. It does not introduce another privileged timer import or advertise the
stronger complete-controller clock contract.

The request states are Idle, Resolve, Submit, Active, Cancel, Drain, Clear,
Done and Retained. Clean completion or proven cancellation can be consumed and
the allocation reused. An unresolved timeout, failed clear, clock/custody fault
or uncertain detach enters Retained. That state pins request storage, claim,
endpoint and controller indefinitely; later callbacks record returned DMA
without changing the result or making cleanup eligible. `step()`, `cancel()`,
`take()` and a new request cannot automatically retry or clear retained custody.
There is deliberately no recovery/reset operation for this private state.

Public legacy event, transfer, claim/release and lifecycle paths refuse entry
while the private request owns storage, before calling an SDK drain or freeing
DMA. HID release/quiescence checks this before its interrupt drains too. Idle
private state leaves the established legacy paths unchanged.

## Exact SDK adaptation

Official source: [ESP-IDF v4.4.7](https://github.com/espressif/esp-idf/tree/v4.4.7),
commit `38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`.
`scripts/stage_usb_bounded.py` checks the complete original host/HCD Git blob
IDs before writing private build copies, and rejects changed input or an
ambiguous patch anchor. The unchanged USBH source was separately inspected. The SDK checkout is never patched in place.

| Source | Verified Git blob |
| --- | --- |
| `components/usb/usb_host.c` | `ecd49ace784afb4bfeed353c001e721ef36a0bec` |
| `components/usb/usbh.c` | `76942870194134dcb5b90269d22291e37db38b8b` |
| `components/usb/hcd_dwc.c` | `1aeb3a1a2f6ed1b2cf00f98513f17607af4dd33d` |

The appended `idf/bounded_*.inc` implementations use try-only host/HCD spinlock
admission. They make no FreeRTOS queue or semaphore calls: even a zero-wait
mutex take/give can reach the global scheduler spinlock through priority
inheritance. The original `usbh.c` remains unchanged; its blob is listed above
as the verified blocker source. Existing HCD helpers are called
only after acquiring their lock, so their nested critical sections are
recursive rather than cross-core waits. The host lock is acquired first and
held across the HCD call, including a stock flush callback into that same host.
The fixed bulk flush keeps an extra recursive HCD level, so the stock helper's
callback release/re-entry cannot drop the last level and spin on another core.
This is scoped to the controller's serialized owner and bounded callbacks.

- Client pumping retires at most one URB from the exact owned bulk endpoint.
  Other endpoints, control completions and client messages remain deferred,
  without starving that owned endpoint behind unrelated queue entries. It never
  calls the SDK's unbounded event drain. Endpoint queue emptiness is observed
  under the HCD lock before clearing its pending flag; a later ISR can requeue
  it after the host critical section exits.
- The two original host queue mutation sites maintain a private queued-message
  count. The bounded path can report unhandled messages without calling a
  FreeRTOS queue API. Its owned pipe callback marks pending work directly,
  skipping semaphore notification. The next legacy submission restores normal
  notification behavior. Both mechanisms rely on this controller's single
  serialized library/client owner; they are not a multi-task SDK API.
- Library inspection preserves all pending HUB/USBH/event flags. It returns
  incomplete for deferred work and an error for a missing library. It does not
  call `hub_process()` or `usbh_process()`, consume an event, or report complete
  bus inventory merely because the client queue is empty.
- Bulk admission checks the sole client's existing claimed-interface records
  with a maximum 16-interface/30-endpoint scan. These records are mutated only
  by that same serialized task, while ISR state is protected by the host lock.
  It never calls the USBH mutex-backed lookup. The actual HCD enqueue owns its
  URB and DMA. No caller-owned buffer is used.
- Cancellation requests a real HAL halt and returns immediately. A new private
  HCD bit retains command custody while the real halt ISR is outstanding. The
  ISR acknowledges that halt without task-notification waiting. An error ISR
  that wins the race also acknowledges the halt only after the stock HAL has
  decoded the channel-halted error; its obsolete halt-request flag is cleared.
- A queued callback or a detached port is not halt proof. The original HCD
  command/free paths reject a private pending halt. The request must observe
  halt acknowledgement, callback retirement and successful endpoint clear
  before reporting clean cancellation.
- Stock flush work is admitted only for one non-ISO bulk URB and two fixed DMA
  buffers. The pending-URB and ISO loops cannot grow under this restriction.
  Normal client dequeue is constant-work and never runs a variable flush for
  another endpoint type.

All other SDK functions preserve their original implementation. The
existing 500 ms legacy drain is still a legacy operation, and is never used by
the new private path. Its presence is one reason this is not a full bounded
controller implementation.

## Remaining interfaces

The full controller suffix remains absent until the following reached paths
are implemented and verified as bounded native steps:

- Root-port debounce/reset/resume, enumeration, HUB and complete USBH device
  processing, including disconnect/EP0 recovery and device free
- Host install/uninstall, client registration/deregistration and device close
- Claim/release endpoint allocation and shared SDK mutex admission
- Complete cached-configuration/claim inventory flow under one caller budget
- Control/EP0 transfer ownership and cancellation
- Role switching, PHY startup/cleanup, VBUS/power admission and restoration
- The full monotonic clock/lifecycle contract and a complete bounded inventory
  callback, including every retained failure path

This slice assumes an already installed host and a previously established live
claim. It cannot create either through a bounded route yet. Physical X4
VBUS/PHY/pin/electrical adoption remains separate and unqualified. There was no
hardware operation, firmware build/flash, release, Runtime edit or product edit.

## Verification and reproduction

Implementation base: Reader `0eb846caa872bcb1bbe6952ebb9640e495213baa`.
On 2026-10-08 live Reader master and the published release index both carried
`usb-controller-esp32s3` 0.1.20. The checked index blob was
`9da276182991cf808625d76b8664f7fa5bb01d5e`; this unit advances only that package
to 0.1.21.

The normal target builder is still
`python scripts/probe_usb_controller_esp32s3.py --link-experiment`.
For a sparse checkout, generate a real ESP32-S3 PlatformIO compilation database
from `test/target/usb_controller_bounded` and pass its `compile_commands.json`
through `--compile-database`. An existing exact sparse SDK checkout can be
provided through `--idf-source`. This fixture is compile-only and has no boot
entry point. Use the shared pinned GCC 8.4.0+2021r2-patch5/PlatformIO installation
and set `PLATFORMIO_SETTING_ENABLE_TELEMETRY=No`; do not duplicate toolchains.

Set `USB_CONTROLLER_IDF_SOURCE` to that exact SDK checkout, then run:

```sh
bash test/run_usb_controller_bounded_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_controller_bounded_test.sh
bash test/run_usb_host_v2_test.sh
bash test/run_usb_hid_test.sh
python scripts/audit_usb_controller_elf.py --strict
python scripts/build_installed_usb_stack.py --ids usb-controller-esp32s3
```

The focused harness compiles the actual owned-request header, appended SDK
steps, exact staged HCD ISR, flush/clear, dequeue/enqueue and DMA-retirement
bodies. OS contention, hardware registers and buffer fill/execution are
simulated. It verifies 31 scenarios: buffer lifetime and copy-out, try-lock
admission, missing/deferred library work, preserved message floods, cancellation
before/after completion, halt/error IRQ races, original-budget cancellation,
missing/late callbacks, detach, failed clear, retained cleanup refusal, malformed
length/MPS, interface scope, finite flush admission, flush-lock retention, legacy message
counting, duplicate callbacks in Done and Drain, clock
reversal/overrun and deadline overflow. Changed upstream source is rejected
before staging. Both normal and ASan/UBSan runs passed with UBSan configured to halt on error.

The existing host/class/deadline and HID/input/role suites, actual startup
ordering helper and production cleanup body also passed. Xtensa ET_DYN
compilation/link and strict import/relocation validation passed with only
`t5_driver_get` exported and 47 generic imports; no USB implementation is
imported from Runtime. Source manifest, ordinary package, provider metadata,
archive and generic catalog agree on 0.1.21. Outputs are local validation
artifacts, not a release or proof of physical USB behavior.
