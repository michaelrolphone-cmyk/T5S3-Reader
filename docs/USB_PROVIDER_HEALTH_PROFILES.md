# Explicit USB provider health profiles

This source unit adds production, opt-in `risc_provider_health_v1_descriptor`
objects for the existing controller/host/input stack. It does not activate USB,
select a product configuration, qualify physical X4 VBUS, or change an existing
capability prefix. The fixed header is copied verbatim from Runtime
`51a9a09e15789be4587f59295f89c3e3b5be0c66` (0.1.94), SHA-256
`0d36bbac73631765462c79de368469c29cac1b4e85c68cb9c7844e3611804c1f`.

The base is Reader `bf997f3d777e644e98c2f885a9c9f4c81e1a0e48`, whose source tree
is also published as `c334919a05d393b7b3a536fceb117cfbb573a4ef` in PR468. Its
evidence-only public successor is `59177418ea746c7605b7b5a5aec501e70bdae80f`.
No blocked CI-only changes, device-MSC work, serial .87 work, or PR350 changes
are part of this unit.

## Explicit selection and versions

Each changed distributable has a separate `manifest.health.json` with the same
stable package identity and a higher numeric version. Normal manifests, driver
translation units, and default class builders remain unchanged. Live master,
USB package tags, and the relevant open USB PRs were checked on 2026-10-09 before
these versions were allocated. No new publication was performed.

| Stable ID | Unchanged default | Selected health profile |
| --- | --- | --- |
| usb-controller-esp32s3 | 0.1.23; native-PHY opt-in 0.1.24 | 0.1.25 with native PHY |
| usb-host-v2 | 0.1.6 | 0.1.7 |
| usb-hid | 0.1.2 | 0.1.3 |
| usb-hid-keyboard | 0.1.1 | 0.1.2 |
| usb-hid-mouse | 0.1.0 | 0.1.1 |
| usb-hid-gamepad | 0.1.4 | 0.1.5 |
| usb-xinput-gamepad | 0.1.3 | 0.1.4 |
| usb-hid-text-input | 0.1.0 | 0.1.1 |

The seven class/host health translation units include their unchanged production
`driver.c`. The controller build stages its unchanged final `driver.cpp` with
exactly one checked replacement: the outer getter is renamed, allowing the
selected wrapper to return its observed dependency table. The original physical
controller, interrupt layer, and private IDF adaptation still execute. Default
controller builds do not stage or include this wrapper.

## Observation and ownership

Health checks are bounded local reads. They do not poll, do I/O, allocate, log,
clean up, query a dependency, call a native gateway, or reenter the graph. The
controller's internal `risc_usb_*` health getters read the same ELF's own private
state; they are linked locally, not imported native services. False cleanup
latches are written at the original operation boundary, never by `check()`.

READY concerns safe survival of a transition, not permission to unload or a
claim that a physical device is working. Live copied subscriptions and live
controller-owned interrupt DMA can be READY while `quiesce()` correctly refuses
unload. Runtime must still prove exact surviving grant ownership and inspect
every active module in the connected dependency/dependent component.

| Provider | Owned data and callbacks | Refusal conditions |
| --- | --- | --- |
| Controller | Own client/event queue, IDF state, DMA, static interrupt callback/context, copied private control/bulk buffers, native PHY/VBUS tokens | Failed cleanup or lost ownership record: RETAINED. Submitted legacy transfer surviving its synchronous call: RETAINED. Private request awaiting ordinary progress/take: BUSY unless terminal. Event/role fault without demonstrated lost custody: UNKNOWN. |
| Host | Own device/claim generations and scratch descriptors; pinned controller API | Failed legacy release, closing claim, deadline-retained state, or failed lower quiesce: RETAINED. Inventory fault: UNKNOWN. |
| Raw HID | Copied interface/session records and opaque host claims; no stored consumer buffers | Local fault: UNKNOWN. Lower-host health is independently required. |
| Mouse | Copied relative events, layout, snapshots and raw-session identity | Unattempted queued closes: BUSY. A false raw close or terminal provider state: RETAINED. |
| Keyboard/HID gamepad | Copied key/event queues or current-state mailboxes; owned layout/session data | A false raw close: RETAINED; gamepad's retained rejected-interface close is also visible. |
| XInput | Copied protocol state/mailboxes and opaque host claim | Lower-host health remains mandatory after legacy void release. |
| Text | Owned translation state and copied queues; opaque keyboard subscription | False keyboard unsubscribe: RETAINED. |

The selected keyboard, mouse, HID-gamepad and text modules copy the consumed
dependency API prefix into their own storage. They preserve the original
context and callbacks, replacing only cleanup with an observer. Advertised
sizes are bounded to the copied storage. A rejected second start cannot overwrite
an active facade. The controller does the equivalent for the consumed VBUS
monitor/external-host prefix and native PHY API. Host exposes wrappers at the
existing release/quiesce offsets. These observers return the original result
and preserve the original cleanup order and behavior.

A failed legacy bool release conveys no proof of reversible custody, even if
its old API supports retry. It therefore reports RETAINED in this profile.
Runtime's terminal fence stops subsequent calls into the affected mappings.
Separate compatibility fixtures show that an old cleanup retry can still work,
and cannot erase a selected profile's observed terminal failure.

The controller distinguishes completed-STALL recovery from failed DMA drain:
an endpoint-clear rejection after returned DMA does not manufacture custody
loss. Likewise, direct quiesce can park the role and refuse fully tracked live
claims without attempting their release; that intact state is BUSY. A later
partial cleanup failure outranks a prior event-fault UNKNOWN. Successful device
open or interface claim followed by exhausted token allocation is RETAINED;
pre-admission exhaustion is UNKNOWN, and a rejected descriptor whose exact
unassigned handle is successfully closed remains safe.

## The hidden raw-HID cleanup case

The real raw-HID `close()` calls the host's legacy **void** `release`, clears its
session and returns true. The real host can simultaneously retain a closing
claim because `controller.release()` returned false. The mouse may therefore
report successful semantic close while its host remains unsafe. The new host
health descriptor reports RETAINED and Runtime refuses handoff, preserves the
mapping/dependency custody, and makes no further provider calls. No class-level
bool result substitutes for that graph-wide proof.

## Complete component boundaries

The selected forward paths are:

- Text → keyboard → raw HID → host → controller
- Mouse/HID gamepad → raw HID plus platform.clock
- XInput → host plus platform.clock
- Controller → board.power.vbus plus native platform.usb.phy.resource

Native platform API tables do not require ELF descriptors; they still need
Runtime's exact owner/resource fences. An ordinary `platform-clock-v1` ELF does
require a descriptor and is **not** selected by this unit. Its active sibling
users can also join the component, including display, SD, I2C, touch and power
providers. No missing ELF descriptor is silently treated as native or safe.

The only board-power implementation in this source is the T5S3 BQ25896 provider:
`board-power-t5s3-v2` → i2c.bus + platform.clock + board.power.bq25896.profile.
Those actual mapped ELFs and their active connected dependents need health
profiles for a complete live closure. The T5S3 electrical profile does not
establish X4 VBUS source/sense topology. There is no invented X4 GPIO, board
power root, or powered-host qualification here.

Unselected reverse consumers include usb-ui-navigation (through text/HID and
XInput gamepad), CDC, CH34x, CP210x, FTDI, MSC, MSP, STLink and serial-witness
providers. If any is active in the connected component, its missing health
descriptor yields UNKNOWN. This task does not reopen those implementations.
The normal .94 products select no production USB health profile yet.

## Reproduction and evidence

Use an existing pinned ESP-IDF 4.4.7 checkout and the verified ESP32-S3
NativeUsbBridge compilation database. No network download or device is needed.

```sh
python3 scripts/probe_usb_controller_esp32s3.py --link-experiment \
  --native-phy-lease --provider-health \
  --compile-database /path/to/compile_commands.json \
  --idf-source /path/to/esp-idf-v4.4.7
python3 scripts/build_usb_provider_health.py --cc /path/to/xtensa-esp32s3-elf-gcc \
  --ids usb-controller-esp32s3 usb-host-v2 usb-hid usb-hid-keyboard \
  usb-hid-mouse usb-hid-gamepad usb-xinput-gamepad usb-hid-text-input
bash test/run_usb_provider_health_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_provider_health_test.sh
USB_HEALTH_RUNTIME_SOURCE=/path/to/runtime-0194 \
  bash test/run_usb_provider_health_runtime_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 USB_HEALTH_RUNTIME_SOURCE=/path/to/runtime-0194 \
  bash test/run_usb_provider_health_runtime_test.sh
python3 test/provider_health/default_identity.py \
  --cc /path/to/xtensa-esp32s3-elf-gcc \
  --compile-database /path/to/compile_commands.json \
  --idf-source /path/to/esp-idf-v4.4.7 --receipt /path/to/default-identity.json
python3 test/provider_health/audit_test.py \
  dist/provider-health/packages/usb-controller-esp32s3/driver.elf \
  dist/provider-health/packages/usb-hid-mouse/driver.elf
```

Python package/audit commands require pyelftools. Selected packages and the
catalog are emitted beneath `dist/provider-health`; ordinary outputs are not
selected or relabelled. `audit_provider_health_elf.py` checks the exact const
object, declared size/version, permitted readonly mapping, local executable
callback and its RELATIVE relocation. Controller scoped imports and all loader
relocations receive the existing strict audit with explicit health selection.
Sixteen malformed target controls reject altered descriptors/callbacks/storage.

The source fixture executes the complete production controller against modeled
IDF/RTOS/VBUS/PHY boundaries, plus actual host/raw-HID and all semantic shared
modules. It covers live copied input, owned armed DMA, bounded private-request
states, failed cleanup, token exhaustion, empty/stopped state, and readonly
observation. Runtime .94 executes the real production host/HID descriptors for
healthy handoff, missing-descriptor refusal and hidden-release retention.
Address/undefined sanitizers run with fatal errors; leak detection is disabled
because terminal retained scenarios intentionally keep unsafe mappings/storage.
These are software/control-flow proofs, not physical USB/electrical tests.

The flag-off comparator rebuilds original and current entry points at identical
source paths and compares **complete ELF bytes**, including both controller .23
and native-PHY .24, plus seven host/class profiles. All 215 original tracked
Driver/SDK inputs are checked byte-identical. These receipts are new reproductions;
existing frozen artifacts and their original build paths/hashes remain unchanged.

## Remaining integration

A product still needs a qualified board-power source/sense root, health coverage
for the actual complete active component, Runtime import/admission/selection
binding, and exact long-lived grants owned outside departing invocations. It
also needs a bounded owner pump that services host/role and class work, preserves
keyboard/text event order and gamepad current state, and surfaces mouse GAP
recovery. Active native ownership or a READY class descriptor alone supplies
neither the pump nor this complete graph proof. No product activation, device
operation, flash, publication, or powered-host claim is included.
