# Private bounded configuration and claim admission

Controller `usb-controller-esp32s3` 0.1.22 extends the preserved 0.1.21 checkpoint
`a00f5a8a7c07465cc2f7c510312655a919650254`. It supplies private owned operations for
cached configuration, native interface acquisition, checked release/rollback,
and once-only device reference closure. It still publishes only the existing
52-byte interrupt/diagnostic prefix. No deadline capability is advertised.

This is an explicitly prepared, already-installed-host slice. It does not
complete physical Serial Monitor admission or make the remaining controller
lifecycle, role, power, enumeration or EP0 operations deadline-qualified.

## Resource prerequisite and measured cost

`prepare_owned_admission()` is the sole private entry to
`risc_usb_admission_prepare()`. Preparation explicitly allocates internal memory
and aligned DMA storage; it has **no deadline guarantee**. It is never called by
configuration/claim/release/close, and is not a hidden legacy fallback. Missing
preparation makes native claim admission fail cleanly. Cached configuration and
logical reference close need no pool allocation.

The pool preserves the controller's 16-claim limit and the actual SDK's eight
hardware channels. An existing EP0 or legacy pipe occupies a real HAL channel;
preparation does not reserve or bypass it. Claim setup supports bulk and
interrupt endpoints. Unsupported endpoint types, invalid intervals, malformed
descriptors and channel exhaustion fail without replacing existing claims.

Measured from the GCC8 Xtensa target ELF's compiled size records:

| Requested storage | Bytes |
| --- | ---: |
| Host/interface/endpoint metadata and one 4096-byte parser workspace | 7,232 |
| Eight HCD pipe/channel/two-buffer metadata slots | 1,184 |
| Internal DMA arena: eight pipes × two 512-byte aligned descriptor strides | 8,192 |
| Total explicitly prepared internal allocation | 16,608 |
| Separate controller owned-request object in the ELF | 4,168 |

The DMA arena is allocated with both INTERNAL and DMA capabilities; metadata
uses INTERNAL and 8BIT. Static ELF placement is never treated as proof of DMA
capability. Each descriptor stride holds the pinned SDK's bounded 32-descriptor
interrupt list; smaller bulk lists reuse it. The table reports requested
storage, excluding allocator overhead and the small global pool pointers/flags.
The existing 4096-byte bulk payload allocation is reused, not duplicated.

Explicit disposal remains an unqualified lifecycle operation. It refuses any
claim or retained callback custody, checks HCD slots are all idle, and only
then frees preparation memory. Controller cleanup invokes it after host
uninstall and before PHY/VBUS release. Preparation/disposal and startup memory
admission remain named prerequisites for the eventual full deadline contract.

## Owned operation and SDK boundaries

`OwnedAdmissionRequest.h` retains copied operation parameters and results, never
an application output pointer. Each `step()` performs one bounded native action
and returns to the existing cooperative owner. A claim parses one descriptor or
acquires one endpoint per step; release retires one endpoint per step. One
absolute 1..1000 ms budget includes setup, progress and rollback. Claims reserve
the final `min(25, budget/4)` ms for rollback within that same budget.

- Configuration copies at most 4096 bytes from a referenced configured device
  under try-only host/USBH admission. Device address and the client's opened
  bitmap are checked first. The eventual caller receives an owned copy through
  `take()`, including a bounded capacity check.
- Claim identity is reserved in the controller before acquisition. The SDK pool
  holds that exact token and copies interface/endpoint descriptors into its
  own records. Descriptor parsing has one cursor, rejects duplicate endpoint
  addresses and ambiguous interfaces, and never rescans per endpoint.
- Private host/USBH maps use the existing single serialized library/client
  owner. ISR-shared state uses try-only host → USBH → HCD critical admission.
  No bounded operation calls a FreeRTOS queue, semaphore, allocator, logger,
  fixed drain or task-notification wait. Even zero-wait mutex calls were
  excluded because priority inheritance can acquire scheduler spinlocks.
- Real HAL channel allocation obeys the current eight-channel occupancy and
  FIFO/speed/interval constraints. Acquisition installs a USBH endpoint map
  only after HCD success. Partial failure retains every acquired pipe until
  explicit bounded rollback proves retirement.
- Release checks host callback/in-flight state, exact USBH endpoint mapping,
  HCD commands/reset/halt state, task waiter, hardware-active flag, URB queues
  and DMA buffer execution/parsing state. It clears maps and returns slots
  only after those proofs. The published interface remains tracked until its
  final metadata removal, even if all physical channels were already freed.
- Pool-backed interfaces and pipes reject the original heap-free routes.
  Controller legacy entry points reject active private ownership; they cannot
  route a timed request through an old blocking claim, close or drain.
- Each endpoint callback carries a bounded, monotonically assigned generation
  cookie. Stale cookies or pipe/slot mismatches fence the pool. Exhaustion
  rejects admission rather than wrapping. Neither disposal nor provider
  restart within one loaded image resets cookie history.
- Clock faults, failed retirement, incomplete rollback and late completion
  retain exact custody. Once Retained, the request and SDK claim refuse retry,
  reuse and disposal. A stale result cannot remove another controller claim.

Pool preparation and allocation failure before physical acquisition are clean
failures. An unpublished claim with zero acquired endpoints can discard its
private metadata without a lock, allocator or physical cleanup when its budget
expires. Every state with physical or published-interface custody instead
requires proved release or retains its exact claim.

## Device close is not physical destruction

The bounded close verifies that no interface, partial native claim or client
control transfer still owns the reference. It checks pending USBH work before
decrementing: the pinned `_dev_set_actions()` does not append flags to a device
already on its pending list.

Reference decrement and clearing the host's opened-device bitmap happen once.
If last-reference closure schedules device free/recovery or port disable, the
host retains those exact pending flags without calling an OS notification. The
operation reports retained deferred cleanup and records that the reference
was already closed. Reentry cannot decrement it again or treat scheduled work
as completed destruction. Combined release/close also preserves the original
claim token when its interface was released but device cleanup remains.

Even clean logical reference closure does not prove SDK device destruction,
host shutdown, or safe provider unload. Those are separate remaining paths.

## Exact source and remaining work

The builder stages private adaptations from official ESP-IDF v4.4.7 commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`, retaining the .21 source pin checks:

| File | Original Git blob |
| --- | --- |
| `usb_host.c` | `ecd49ace784afb4bfeed353c001e721ef36a0bec` |
| `usbh.c` | `76942870194134dcb5b90269d22291e37db38b8b` |
| `hcd_dwc.c` | `1aeb3a1a2f6ed1b2cf00f98513f17607af4dd33d` |

SDK caches are read-only; source drift or ambiguous patch anchors reject the
build. Adaptations remain inside the physical controller ELF, with no Runtime
USB bridge, new firmware authority, worker task, board/profile change or public
API-prefix change. Legacy defaults are unchanged while private state is idle.

The full suffix remains absent pending bounded memory/startup admission,
complete HUB/USBH enumeration and device-free processing, root-port timing,
EP0/control cancellation, role/PHY/VBUS setup and cleanup, full event inventory,
and the complete monotonic clock/lifecycle contract. Physical X4 electrical and
board adoption remains separate and unqualified.

## Validation

The production owned-request classes and controller result-consumption body
run against exact staged SDK admission, list/map, refcount, HCD pool, callback
and retirement code. OS locks, heap failure and hardware/HAL events are
simulated. Both normal and ASan/UBSan runs passed, with UBSan halting on errors.

Forty-nine admission scenarios cover pool allocation failures, clean absence,
configuration copies, claim/channel exhaustion with existing EP0, partial
acquisition, cancellation/deadline rollback, every modeled HCD/host retirement
refusal, final metadata admission failure, descriptor ownership, stale tokens
and callbacks, device-close idempotence, deferred free/recovery, combined
release/close, and native claim → owned bulk completion → checked release.
The preserved 31 bulk scenarios, production cleanup faults, legacy host/class
and HID/input/role cases also pass. Target compile/link, exact 52-byte public
prefix, strict import/relocation audit and ordinary package identity checks
provide software evidence only.

Reproduce with the shared pinned GCC8/PlatformIO tools, telemetry disabled, and
an exact SDK checkout supplied as `USB_CONTROLLER_IDF_SOURCE`:

```sh
bash test/run_usb_controller_admission_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_controller_admission_test.sh
bash test/run_usb_controller_bounded_test.sh
```

Target compile and package commands are the same as the .21
[bounded-slice instructions](USB_CONTROLLER_BOUNDED_SLICE.md), using this
worktree's builder/output. No publication, firmware build/flash, hardware
operation or electrical test was performed. Independent .22 review is pending
central integration review because a separate review worker was unavailable;
the tests do not constitute genuine multicore or hardware concurrency proof.
