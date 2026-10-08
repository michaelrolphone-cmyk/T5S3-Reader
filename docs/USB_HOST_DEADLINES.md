# Optional bounded USB host operations

## Implemented slice

`usb-host-v2` 0.1.6 implements the existing `risc_usb_host_deadlines_v1` suffix
without changing that class-facing contract or any deployed host prefix offset.
It advertises the suffix only after binding a controller with the complete,
size/tag/version/function-checked `risc_usb_controller_deadlines_v1` suffix.
A legacy, truncated, wrongly tagged or incomplete controller keeps the host's
advertised size exactly `sizeof(risc_usb_host_snapshot_v1)`, with no deadline
pointer or tag. The shipping ESP32-S3 controller 0.1.20 does not supply this
contract, so tagged serial open remains UNSUPPORTED on that physical stack.
This is a host implementation with an explicit native blocker, not a completed
physical Serial Monitor backend.

The source remains in Reader's existing shared host driver. There is no CDC or
vendor-class edit, Runtime import, native SDK fork, new worker task, X4 product
change, PR350 change, publication or hardware operation in this slice.

Each optional operation starts one positive 1..1000 ms caller budget. The host
passes only its remainder to every lower event, configuration, claim, transfer
or release callback. Configuration, claim and claim-scoped control/bulk calls
refresh inventory under that same budget before using a generation token.
Descriptor parsing is limited to 4096 bytes; device/claim scans retain their
8/16 bounds. No lower legacy callback is used by the optional path. Existing
claim-scoped control/bulk signatures use the new controller callbacks when the
host advertises deadline support.

Inventory consumes at most 16 events. Success requires observing empty from
the bounded controller callback. Reaching the event count without that proof
returns IO and copies no inventory, even if the sixteenth event happened to be
the last event. A later bounded call may finish draining. This avoids reporting
a partial set as complete, and distinguishes interrupted/failed polling from
an empty healthy bus. Capacity failure publishes the required count and no
partial token records. Generation identities continue across detach/reconnect
and successful stop/start in one loaded image.

A new host token is reserved before physical claim acquisition. Partial setup
returns its exact surviving host claim when the controller supplies one;
unknown custody still retains the dependency. Failed release, malformed claim
success, lower RETAINED, clock reversal and observed contract overruns fence
further work. Retention is sticky: lifecycle cleanup and legacy release cannot
retry a timed claim or discard retained custody. A successful timed release is
the only path that clears that timed claim. A clean cancellation/timeout before
physical acquisition returns zero and preserves already acquired independent
claims for the caller's bounded rollback. Observing an overrun is a fault check,
not an assertion that an unbounded callback was made safe.

## Verified native blocker

Reader's physical-controller builder pins ESP-IDF v4.4.7. Its actual SDK sources
were read on 2026-10-08, including these blob identities:

- `components/usb/usb_host.c`: `ecd49ace784afb4bfeed353c001e721ef36a0bec`
- `components/usb/usbh.c`: `76942870194134dcb5b90269d22291e37db38b8b`
- `components/usb/hcd_dwc.c`: `1aeb3a1a2f6ed1b2cf00f98513f17607af4dd33d`

The bound cannot be supplied by the current SDK entry points:

- [Host claim and release](https://github.com/espressif/esp-idf/blob/v4.4.7/components/usb/usb_host.c#L1101)
  take the host mutex with `portMAX_DELAY` (lines 1115 and 1152). Device close
  does likewise at line 837. These APIs accept no deadline.
- [Cached configuration retrieval](https://github.com/espressif/esp-idf/blob/v4.4.7/components/usb/usb_host.c#L912)
  delegates to `usbh_dev_get_config_desc`; that particular cached lookup uses
  a critical section, but claim's endpoint allocation/free and endpoint context
  access still contain unbounded USBH mutex acquisition. A cached descriptor
  alone does not make the complete claim/transfer/release path bounded.
- [Client event processing](https://github.com/espressif/esp-idf/blob/v4.4.7/components/usb/usb_host.c#L718)
  applies `timeout_ticks` only to initial waiting. Its processing loops consume
  newly arriving callbacks/events without a work or elapsed-time limit. The
  library event handler similarly loops while processing flags remain set.
- [HCD internal waits](https://github.com/espressif/esp-idf/blob/v4.4.7/components/usb/hcd_dwc.c#L692)
  use `ulTaskNotifyTake(..., portMAX_DELAY)` for port and pipe ISR completion.
  Endpoint halt reaches these paths through `hcd_pipe_command`. Root-port
  debounce/reset/resume also contain fixed delays with no caller budget.
- Reader's `driver_base.cpp` adds a fresh 500 ms drain after transfer timeout;
  `next_event` can run role startup/cleanup and power operations. The caller's
  timeout therefore does not cover all work. An EP0 control timeout may leave
  the transfer in flight. Owned DMA is retained by the existing cleanup paths,
  but the host must not claim a bounded physical completion contract from them.

The existing physical controller and its version are unchanged. Its established
legacy behavior, power/PHY ordering and cleanup retention tests remain intact.

## Smallest remaining native contract

The new controller suffix requires a monotonic, nonblocking millisecond clock;
a bounded event step that can prove inventory complete; deadline-aware cached
configuration access, claim acquisition and checked release; and control/bulk
operations whose total budget includes lock admission, transfer submission,
event pumping and cancellation. No callback may retain a caller pointer.
Timed-out/cancelled requests whose physical completion cannot be proved must
report RETAINED and keep controller-owned request, DMA, callback and claim state.
The suffix must remain absent until every reached native/role/power path meets
these requirements.

A controller-owned asynchronous request/state machine can bound caller return
without an app/driver task or broad Runtime imports, but only if the SDK/HCD
entry points it calls become nonblocking, bounded steps. It can copy requests
into fixed-capacity controller storage and advance them from the existing owner
poll using absolute deadlines and explicit work limits. Mutex acquisition would
be try/defer; port reset/debounce and halt/flush would be persistent states
advanced by ISR completion and timestamps, not task-notification waits. Device
and endpoint ownership must survive every incomplete state and partial failure.
Simply scheduling today's blocking SDK call from that owner poll still blocks
the owner and cannot establish the contract. Moving it to a hidden worker also
would not solve cancellation or custody, and is outside this slice.

The current synchronous class-facing suffix permits a safe early RETAINED
return with copied pending work, but that fences the session and pins its
provider. It does not authorize an automatically retried, later-successful close.
Ordinary recoverable timeouts require proven cancellation within the same
budget; reusable pending operations would need a separately reviewed async
request/completion contract. No such SDK rewrite or new recovery ABI is claimed.

## Source/version and verification

Implementation base: Reader `134d56edec35dda20de016ff01bca4c756af4c4c`.
Live Reader master and the published `release-index.json` both identified
`usb-host-v2` 0.1.5 on 2026-10-08; the index blob checked was
`9da276182991cf808625d76b8664f7fa5bb01d5e`. This slice advances only that package
to 0.1.6, reserved with the parent integration task.

Run `bash test/run_usb_host_deadlines_test.sh` and
`SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_host_deadlines_test.sh`.
Twenty production-host ELF scenarios exercise capability admission, complete
legacy prefixes, remaining budgets, partial claim/setup, cancellation, timeout,
clock reversal, stale generations, event-count/elapsed limits, inventory faults,
physical release refusal, uncertain DMA retention, control authorization and
legacy-release isolation. Only the lower controller is simulated; this does not
qualify native USB or electrical behavior. Retained scenarios intentionally
keep the mapping alive until process exit.

`bash test/run_usb_host_v2_test.sh` covers the unchanged legacy host/class
integration and existing production controller cleanup policies. The target
builder is `scripts/build_usb_host_v2.py`; packaging selects only `usb-host-v2`
through `scripts/build_installed_usb_stack.py --ids usb-host-v2`.
No full firmware build, physical attach/unplug test, X4 PHY/VBUS/pin/electrical
qualification or diagnostic-restoration proof is supplied by these checks.

Passed on 2026-10-08: all twenty deadline scenarios, normal and ASan/UBSan;
existing host/class/controller cleanup suite; Xtensa ET_DYN build and relocation
map audit; ordinary package, source manifest and catalog identity/version
agreement at 0.1.6. The target imports only `memset` and exports only
`t5_driver_get`. Build outputs are local, unpublished verification artifacts.
