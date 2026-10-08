# Private bounded EP0 transfers and callback dispatch custody

`usb-controller-esp32s3` 0.1.23 extends the clean 0.1.22 source checkpoint
`5a8d31159258dbecbf3ee4c33bd8ce9dddc30158`. It implements a private owned control
request and native EP0 submission, completion and cancellation. It also closes
the decoded-callback dispatch window identified during central review of the
new control path and the prepared endpoint pool. The public controller still
advertises exactly the existing 52-byte interrupt/diagnostic prefix. The full
deadline suffix remains absent.

## Operation and ownership

The controller reserves a monotonically increasing operation token, distinct
from the device token, before accepting a request. `OwnedControlRequest.h`
copies the eight-byte setup and up to 4096 OUT bytes into the existing
4104-byte controller DMA allocation. It retains no application buffer or output
pointer. IN data is copied only by a successful exact-token `take()`. Results
report payload bytes, excluding setup; short IN is preserved. A wrong token
cannot cancel, advance or consume another request.

The precondition is one serialized owner, an installed host, a registered and
opened configured device, the existing allocated transfer, and an idle native
default pipe. Installation, enumeration and preparation remain explicitly
unqualified. No step allocates, creates a task, calls a FreeRTOS queue/semaphore,
logs, dispatches general HUB/USBH work or falls back to the legacy API.

One absolute 1..1000 ms budget includes submission, progress, cancellation,
dequeue, callback restoration and result completion. Its last
`min(25, budget/4)` ms is reserved for cancellation. Each step tries host → USBH
→ HCD admission once, performs fixed work, then returns to the cooperative
caller. Lock contention preserves the operation without restarting its budget.
Missing library state is an error; deferred library/client work stays deferred
and cannot be reported as an empty healthy bus.

Native submission increments the real USBH control-in-flight count and lends
the existing default pipe to the exact request. It checks the original SDK
callback and argument, pipe type/MPS, commands, queues and DMA state before
installing a generation-checked callback. Failed submission rolls back the
count, host in-flight flag and callback pair. The copied setup follows the
SDK's real setup/data/status machinery, including skipped data and short IN.

Completion requires the real pipe callback, exact done URB, inactive hardware,
empty pending/execution/parsing state, and no command/reset/halt custody.
Dequeue and callback restoration are separate steps. The USBH count and host
in-flight flag remain held between them. Restoration verifies the saved pair,
clears a halted pipe only through the finite native clear, and acknowledges
the transfer callback once after all locks are released.

Cancellation requests a real HAL channel halt. Only the actual halt/error IRQ
can acknowledge an active DMA channel. The single-URB flush retains an extra
recursive HCD lock level around the SDK's callback handoff, so its reentry
cannot become a cross-core wait. A software deadline, detach or notification
does not invent hardware completion. The private BNA error maps to an error
URB after the real halted-channel IRQ, avoiding the stock parser's BNA abort;
the original event remains visible to the private callback. Legacy BNA behavior
is unchanged outside private EP0 custody.

If the deadline expires with DMA, callback, SDK count or restoration custody
unresolved, the request becomes sticky Retained. Late callbacks never thaw it.
No automatic retry, new request, legacy operation, close, provider teardown or
DMA reuse may bypass that fence. A deadline observed after a fully restored
callback can return a clean timeout because physical and callback ownership
has already been proved returned.

## Decoded callback race and the .22 limitation

The pinned `intr_hdlr_main()` releases `hcd_lock` before reading and calling
`pipe->callback` with `pipe->callback_arg`. A control pending flag alone does
not describe another already-decoded event awaiting dispatch. The same window
could read a new generation's callback argument if a .22 pooled endpoint were
retired and reused while dispatch was paused. Earlier saved-callback tests
did not exercise that particular dispatcher window. The preserved .21/.22
source and artifacts are unchanged; this repair is part of .23.

For private EP0 and pool-backed pipes, the actual staged interrupt dispatcher
now snapshots the callback/argument pair and increments a per-pipe dispatch
counter while it still holds `hcd_lock`. It releases the lock, invokes that
exact pair, then decrements only after returning and reacquiring the lock.
The counter saturates rather than wrapping to idle. EP0 dequeue/restoration
and pooled pipe release remain incomplete while dispatch is outstanding;
pool disposal also refuses outstanding dispatch. A duplicate, stale or
post-retirement EP0 callback fences its request. Original dispatch behavior is
preserved for pipes outside these private paths.

This prevents restoring the stock EP0 callback or reusing a pooled slot beneath
a decoded event. It also covers the interval after a callback publishes its
pending flag but before it returns to the dispatcher. The synchronous bounded
flush already holds the extra HCD level and its owner locks throughout its
callback, so it does not need a separate unlocked dispatch counter.

## Measured resource cost

The GCC8 Xtensa artifact contains a 64-byte owned control request, a 40-byte
native control state, a four-byte generation serial and a one-byte sticky
fault flag, excluding alignment padding. The dispatch counter adds four bytes
to each HCD pipe, including the existing EP0 allocation.

The prepared eight-slot HCD pool metadata therefore grows from 1184 to 1216
bytes. Host metadata remains 7232 bytes and internal DMA remains 8192 bytes.
Explicit preparation requests 16640 bytes total, excluding allocator overhead
and small globals. The existing real eight-channel limit and 16-claim limit
are unchanged; EP0 still consumes an actual hardware channel. No additional
DMA buffer or descriptor arena is allocated by control operations.

## Exact source and validation

Adaptations are staged from official ESP-IDF v4.4.7 commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`. Original Git blobs remain pinned:

| Source | Git blob |
| --- | --- |
| usb_host.c | ecd49ace784afb4bfeed353c001e721ef36a0bec |
| usbh.c | 76942870194134dcb5b90269d22291e37db38b8b |
| hcd_dwc.c | 1aeb3a1a2f6ed1b2cf00f98513f17607af4dd33d |

The exact read-only checkout used for both host and target runs is
`/workspace/scratch/c744abbbbd60/reader-usb-controller-bounded-slice/dist/idf-pinned-checkout`.
The older `dist/idf-usb-source/v4.4.7` inspection cache is not a substitute and
correctly fails the byte pin guard. No source hash bypass is used.

Eighty-five normal and ASan/UBSan scenarios execute the production control
request, token/guard wrappers, native steps, dispatcher, exact SDK control
descriptor fill, stage continuation, parse, ISR, halt, flush and dequeue bodies.
They cover copied OUT, zero/short/max IN, every stage's cancellation, completion
races, real error classes, missing/late callbacks, detach, deadline checkpoints,
retirement/restoration failures, lock contention, deferred work, exact tokens,
generation exhaustion, legacy guards and repeated reuse. Dispatcher tests
inject owner progress before callback delivery and after delivery before
dispatch returns, including duplicate/error events, already-retired control,
deadline/overflow fences, and pooled release/reuse. OS/HAL boundaries and
general buffer queue fill/execution are simulated; this is not multicore or
electrical proof. UBSan halts on errors. Intentional retained ownership is
kept until each isolated test process exits.

The preserved 49 admission and 31 bulk scenarios pass normally and sanitized.
Legacy host/class/deadline, HID/input/role, actual cleanup and startup suites
pass. Warning-free target compile/link, 52-byte advertised prefix, all 47
generic imports, 870 supported relocations and ordinary source/package/catalog
identity checks pass. No USB implementation is imported from Runtime.

Live Reader master and release-index checks before .23 reservation showed
published controller 0.1.20; the index blob was
`9da276182991cf808625d76b8664f7fa5bb01d5e`. Source, ordinary package, provider
metadata and archive/catalog identity agree on `usb-controller-esp32s3@0.1.23`.
Artifacts are local qualification outputs, not a publication.

Reproduce the focused checks with `USB_CONTROLLER_IDF_SOURCE` pointing to that
exact checkout:

```sh
bash test/run_usb_controller_control_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_controller_control_test.sh
bash test/run_usb_controller_admission_test.sh
bash test/run_usb_controller_bounded_test.sh
```

Use the shared pinned GCC8/PlatformIO installation with
`PLATFORMIO_SETTING_ENABLE_TELEMETRY=No`. Target, strict audit and package
commands follow [the bounded-slice instructions](USB_CONTROLLER_BOUNDED_SLICE.md).

Remaining work still includes bounded memory/startup admission, HUB/USBH
enumeration and device destruction/recovery, root-port timing, complete event
inventory, role/PHY/VBUS lifecycle and the complete monotonic clock contract.
The private slice does not authorize or qualify physical X4 adoption. Runtime,
board/power policy and products are unchanged. No hardware operation, flash or
publication was performed by this unit. Central review found and requested
the dispatch repair above; independent hardware/multicore validation remains
outside this software checkpoint.
