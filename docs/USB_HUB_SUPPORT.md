# RiscRTE USB Hub and Multi-Device Host Support

## Status and authority

**Normative USB hub support specification.** Read [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), and [USB OTG Host, Hub, and Device-Function Provider Architecture](USB_OTG_HOST_ARCHITECTURE.md) first.

This document defines the required architecture, provider behavior, topology, resource accounting, concurrency, power safety, lifecycle, diagnostics, and acceptance tests for attaching multiple USB devices through one or more USB hubs. It does **not** move USB behavior into the RiscRTE core. The active milestone decides when this specification is an implementation gate; adding this document alone does not claim the behavior is implemented or hardware-qualified.

## 1. Objective

RiscRTE SHALL support multiple simultaneously attached USB devices through an external USB hub while preserving the hardware-agnostic core model.

A valid implementation permits configurations such as:

```text
T5S3 / USB host controller
          |
       USB hub
      /   |    \
keyboard gamepad USB serial device
   |       |          |
input.*  input.*   serial.port
```

Applications SHALL consume semantic capabilities such as `input.keyboard`, `input.gamepad`, `serial.port`, `storage.removable`, or another provider-defined capability. Applications SHALL NOT require special hub-aware code merely because a device is connected through a hub rather than directly to the root port.

## 2. Non-goals

This specification does not:

- make USB or hub topology a built-in RiscRTE core subsystem;
- require applications to enumerate USB buses, ports, descriptors, hubs, endpoints, or addresses;
- grant applications raw `usb.host` authority merely because they consume a semantic USB-backed capability;
- require nested hubs in the first implementation stage;
- require every physically attachable device to be usable simultaneously when controller channels, memory, power, bandwidth, transfer slots, or provider limits are exhausted;
- permit firmware fallback USB behavior if a required provider is absent or fails.

## 3. Architectural invariant

All USB hub behavior SHALL remain inside independently installed USB provider ELFs and their lower-level hardware dependencies.

The generic RiscRTE runtime MAY manage:

```text
capability names
provider dependency graph
execution contexts
generic software leases
generic streams
generic events
bounded copied diagnostics
provider lifecycle
package installation/loading
```

The generic runtime MUST NOT:

```text
parse hub descriptors
enumerate hub ports
power/reset a USB port
interpret USB device addresses
track USB endpoint numbers
allocate USB host channels
schedule physical USB transfers
decide which child belongs to which hub port
recover a USB hub
select a USB class driver
```

USB-specific behavior belongs to the USB provider graph even when generic runtime facilities carry the resulting semantic records, streams, or lifecycle events.

## 4. Provider decomposition

The expected provider graph is:

```text
Application
   |
semantic capability
   |
USB class/provider ELF
   |
usb.host
   |
USB host ELF
   |
usb.controller
   |
board-specific USB controller ELF
   |
optional board.power.vbus
   |
physical controller / root port / external hub / children
```

### 4.1 Controller provider

The board-specific USB controller provider owns:

- controller initialization and shutdown;
- ESP-IDF or equivalent host-stack integration;
- PHY and root-port state;
- physical device open/close;
- controller and host-stack callbacks;
- DMA-safe transfer memory;
- bounded transfer scheduling;
- physical attach/detach events;
- backend hub support activation;
- physical topology observations available from the backend;
- low-level quiescence and callback drainage;
- controller-specific channel/resource limits;
- root VBUS dependency acquisition and release.

The controller provider SHALL NOT publish semantic keyboard, gamepad, serial, storage, or other application-level capabilities.

### 4.2 USB host provider

The USB host provider owns:

- generation-qualified logical device tokens;
- interface and alternate-setting arbitration;
- class-facing configuration-descriptor access;
- logical topology identity;
- parent/child relationships;
- bounded device and interface-claim tables;
- class-facing transfer authorization;
- checked release;
- class-visible device snapshots;
- subtree detach handling;
- resource exhaustion reporting;
- host-level diagnostics.

The host provider SHALL consume controller-provider capabilities rather than call compiled firmware USB behavior.

### 4.3 Hub handling

Hub protocol behavior MAY be implemented:

1. inside the controller/host provider when the underlying USB host stack provides hub support; or
2. in a dedicated privileged hub provider that consumes `usb.host`.

Either implementation is valid only if no USB hub behavior is introduced into generic RiscRTE core code.

A separate application-facing `usb.hub` capability is optional. Basic multi-device operation SHALL NOT require applications to acquire a hub capability.

### 4.4 Class providers

CDC ACM, CP210x, CH34x, FTDI, HID, MSC, debug-probe and other class providers SHALL remain hub-agnostic.

A class provider receives an opaque generation-qualified device identity from `usb.host` and uses the same descriptor/claim/control/transfer contract whether the device is:

```text
root -> device
```

or:

```text
root -> hub -> port -> device
```

Class providers MUST NOT derive authority from a USB address or hub port number.

## 5. Initial supported topology

The first required hub implementation SHALL support one external USB 2.0 hub directly attached to the root host port.

Initial mandatory topology:

```text
root
  |
external hub
  +-- child 1
  +-- child 2
  +-- child 3
  +-- ...
```

Nested hubs are a compatible extension:

```text
root
  |
hub A
  |
hub B
  |
device
```

Nested hubs SHOULD remain disabled until single-level topology, concurrent transfers, subtree teardown, power accounting, and resource exhaustion are validated.

The topology depth limit MUST be bounded and provider-defined. An unsupported depth SHALL fail explicitly rather than partially enumerating a subtree as if it were complete.

## 6. Topology identity

Every live physical USB device SHALL have a generation-qualified identity independent of its USB bus address.

The USB provider shall internally maintain enough topology information to distinguish at minimum:

```text
device token
generation
parent device or root
parent port
topology depth
presence state
identification state
```

Where the backend exposes them, the provider SHOULD also retain speed and hub/port status for diagnostics.

A physical path such as:

```text
root / hub-generation-12 / port-3 / device-generation-41
```

is diagnostic topology, not application authority.

USB addresses, visible VID/PID, product strings, serial strings, and port numbers MUST NOT substitute for generation-safe handles.

## 7. Attach, detach, and generation rules

The following rules are mandatory:

1. Every new physical attachment receives a fresh generation-qualified device token.
2. Reconnecting the same visible peripheral to the same hub port produces a new token.
3. Moving a peripheral to another port produces a new token.
4. A stale token MUST NOT access the replacement device.
5. Detaching one child invalidates only that child's claims, streams, and semantic publications unless shared provider state itself fails.
6. Detaching a hub invalidates the complete downstream subtree.
7. Unrelated root or sibling devices SHALL remain usable when one child is removed.
8. A parent-removal cascade SHALL NOT be treated as a controller corruption merely because child detach events arrive in a different valid order.
9. Event loss, impossible token reuse, unbounded queue overflow, or uncertain physical cleanup SHALL still fail closed and quarantine affected state.

## 8. Hub event model

Hub insertion and removal can produce bursts of device and port events. Event handling SHALL therefore be bounded but burst-tolerant.

Providers SHOULD model equivalent states to:

```text
ATTACHED
IDENTIFYING
READY
DETACHING
GONE
QUARANTINED
```

The exact state enum may remain private.

The event queue MUST:

- have an explicit fixed maximum;
- detect overflow;
- distinguish genuine overflow from a legal detach cascade;
- avoid duplicate publication of identical state;
- preserve enough parent/child ordering information to invalidate a removed subtree;
- never silently drop a child attach or detach;
- never convert an identification failure into a false physical detach.

If provider event coherence is lost, new operations SHALL fail closed until the provider safely reconstructs or restarts its topology.

## 9. Host ABI evolution

Hub support SHALL extend the existing USB provider ABI without invalidating currently installed v1 class consumers.

The existing function-table prefix and current opaque token semantics SHALL remain binary compatible.

New functionality SHALL be added through append-only, `struct_size`-checked extensions or a later API generation that retains a compatible v1 prefix.

The hub-capable host interface SHOULD provide a bounded topology snapshot equivalent to:

```text
device_token
parent_token_or_root
parent_port
depth
presence
identified
optional speed/flags
```

The topology snapshot is for USB providers and diagnostics. Generic RiscRTE device records SHALL continue to expose semantic provider-published identity rather than requiring the core to understand USB paths.

## 10. Multi-device transfer architecture

Enumerating several devices is not sufficient. The controller/host provider SHALL permit useful simultaneous progress across independent devices and interfaces.

A single global transfer object or one controller-wide long blocking transaction MUST NOT become the permanent multi-device architecture.

The controller provider SHALL use a bounded transfer pool or an equivalent bounded scheduler.

Conceptually:

```text
controller transfer pool
  slot 0 -> keyboard interrupt IN
  slot 1 -> gamepad interrupt IN
  slot 2 -> serial bulk IN
  slot 3 -> serial bulk OUT
  slot 4 -> control transaction
  ...
```

Each transfer slot SHALL track enough information to safely bind completion to:

```text
transfer generation
device generation
claim generation
endpoint or control target
direction/type
state
deadline
completion/cancel state
owned DMA buffer
```

The number and size of slots are provider/controller limits, not generic RiscRTE constants.

## 11. Asynchronous transfer safety

If the USB provider exposes asynchronous transfer operations, caller-owned ELF memory MUST NOT be retained across an unbounded callback lifetime.

Preferred pattern:

- OUT submission copies bounded caller data into provider-owned transfer memory before returning.
- IN submission reserves provider-owned receive memory.
- completion is observed through an opaque transfer handle, event, or provider-owned stream;
- result collection copies completed IN data into caller-provided memory during a bounded call;
- cancel and release are generation-checked;
- provider unload is prohibited until all submitted transfers are completed, cancelled and drained.

Persistent direct callbacks from the USB provider into an unloadable class or application ELF are prohibited.

Synchronous convenience calls MAY remain as compatibility wrappers over this bounded internal scheduler.

## 12. Fairness and latency

One slow or idle peripheral SHALL NOT monopolize the host.

Transfer scheduling MUST prevent cases such as a long serial receive timeout blocking all keyboard/gamepad input.

The provider SHOULD enforce:

- bounded per-operation timeouts;
- fair progress among active devices;
- bounded work per scheduler turn;
- cancellation;
- independent RX/TX progress where hardware permits;
- priority sufficient to service latency-sensitive HID without starving bulk transfers;
- backpressure rather than unbounded buffering.

Scheduling policy remains USB-provider implementation, not generic runtime USB policy.

## 13. Host resource accounting

The controller/host provider SHALL explicitly account for finite resources, including as applicable:

```text
device slots
interface claims
endpoint/host channels
transfer slots
DMA buffers
event slots
descriptor buffers
bandwidth/scheduling capacity
power budget
```

Controller-specific limits SHALL be provider/profile data.

A hub having four or more ports does not imply that every possible four-device combination can be active simultaneously.

Resource exhaustion SHALL produce an explicit provider error/state such as:

```text
resource exhausted: device slots
resource exhausted: interface claims
resource exhausted: host channels
resource exhausted: transfer slots
resource exhausted: DMA memory
resource exhausted: power budget
```

Resource exhaustion MUST NOT be reported as a false unplug and MUST NOT corrupt already active siblings.

## 14. Interface and class isolation

Claims remain device- and interface-specific.

The host provider SHALL ensure:

- one class cannot issue interface-recipient control requests against another class's interface;
- device-wide control remains restricted by the existing exclusivity policy or a stronger equivalent;
- one child's failed release does not free or reuse another child's claim;
- one quarantined child does not automatically quarantine healthy siblings;
- a hub control path cannot be invoked through an arbitrary leaf-device claim;
- composite devices behind a hub retain the same interface isolation rules as direct-attached devices.

## 15. Semantic multi-instance publication

Provider publication SHALL support several simultaneous instances of the same semantic capability.

Examples:

```text
two gamepads
two keyboards
two USB serial adapters
keyboard + gamepad + serial
two serial adapters using different chipsets
```

Each semantic instance SHALL retain a distinct generation-qualified device identity and independent physical/provider session.

An application requiring one device MAY use generic selection policy. An application requiring multiple devices SHALL be able to distinguish granted device handles without depending on USB addresses or topology.

## 16. Generic Device Registry behavior

The generic Device Registry SHALL receive provider-originated semantic records exactly as it does for direct-attached devices.

A hub configuration may therefore appear as:

```text
Device A -> input.keyboard
Device B -> input.gamepad
Device C -> serial.port
```

The registry MUST NOT:

- parse the hub path;
- infer parent ports;
- query the USB host stack;
- parse hub or device descriptors;
- reset a port;
- select a USB class.

Provider-published USB topology MAY be shown in diagnostics as copied metadata, but it does not become a core hardware ownership model.

## 17. VBUS and power budgeting

Hub support SHALL integrate with the provider-owned VBUS/power chain.

The USB provider and board power provider SHALL distinguish, where observable:

```text
root VBUS source capability
self-powered hub
bus-powered hub
declared child power demand
available board source budget
overcurrent/fault state
backfeed/reverse-current hazard
```

The first hardware qualification SHOULD use a known self-powered USB 2.0 hub.

Bus-powered hub qualification is separate and requires evidence that the board, charger/boost path, connector, cabling, battery/system supply, and board-power provider can safely source the configured load.

A hub SHALL NOT cause the USB controller provider to silently increase source current beyond the board power provider's granted safe limit.

If required power cannot be safely provided, the provider SHALL reject or depower the affected topology safely rather than brown out the system or leave uncertain VBUS state.

## 18. Hub port power/reset

Port power/reset/recovery operations are USB-provider behavior.

If the backend exposes individual hub-port control, the provider SHALL:

- validate that the target hub generation and port are still current;
- serialize conflicting reset/power operations;
- invalidate affected child generations after a reset that destroys the USB session;
- bound retries and reset duration;
- preserve healthy sibling ports where supported;
- propagate a clear failure if the hub or backend cannot guarantee the requested operation.

Generic applications do not receive raw port-reset authority merely because they consume a child capability.

## 19. Hub removal and subtree teardown

Whole-hub removal is a first-class teardown scenario.

For a hub with multiple active children, removal SHALL:

1. stop new child admissions;
2. mark the downstream topology unavailable;
3. revoke semantic publications/leases for every descendant generation;
4. stop or cancel descendant transfers;
5. drain callbacks/DMA;
6. release descendant interface claims;
7. close descendant physical devices;
8. close/release hub state;
9. preserve unrelated root/sibling devices;
10. allow provider unload only after quiescence is proven.

If any descendant cannot be safely drained or released, the exact affected provider state SHALL remain pinned/quarantined for checked retry. It MUST NOT be returned to an allocation pool as if cleanup succeeded.

## 20. Suspend, sleep, and provider unload

The USB provider graph SHALL not unload while a hub or descendant has:

- an active transfer;
- a pending completion callback;
- live DMA;
- a live interface claim;
- an unresolved port reset;
- uncertain device close;
- uncertain power release.

System sleep or USB provider shutdown SHALL either safely quiesce the complete active topology or fail the transition.

A generic software lease count is not proof that physical USB topology is safe to shut down.

## 21. Diagnostics

USB-provider diagnostics SHOULD expose a bounded topology view equivalent to:

```text
root
  hub token/generation
    port 1 -> keyboard token/generation
    port 2 -> gamepad token/generation
    port 3 -> serial token/generation
```

For each node, diagnostics SHOULD expose provider-known fields such as:

```text
state
generation
parent
port
depth
identified/not identified
claims
active transfers
last error
resource usage
power state where known
```

Aggregate diagnostics SHOULD include:

```text
device slots used/total
claim slots used/total
transfer slots used/total
host channels used/total where available
event queue high-water mark
DMA/buffer usage
resource-exhaustion counters
hub attach/detach count
subtree teardown failures
last topology fault
```

These diagnostics originate in USB providers. Generic RiscRTE diagnostics may display copied provider data without interpreting USB semantics.

## 22. Error model

The hub-capable provider SHALL distinguish at minimum:

```text
physical detach
descriptor/identification failure
unsupported hub/topology depth
device table exhaustion
claim exhaustion
host-channel exhaustion
transfer-slot exhaustion
DMA/buffer exhaustion
power-budget denial
port reset failure
transfer timeout
transfer cancellation
event overflow/coherency loss
uncertain teardown/quarantine
controller failure
```

A zero-device snapshot is valid only when the provider knows there are zero devices. An uncertain topology scan SHALL NOT be published as a healthy empty bus.

## 23. Bounds

All hub support SHALL remain bounded.

At minimum define provider limits for:

- maximum physical devices;
- maximum hub depth;
- maximum hubs;
- maximum interface claims;
- maximum in-flight transfers;
- maximum queued USB events;
- maximum descriptor size;
- maximum per-transfer payload;
- maximum retries;
- maximum reset/recovery duration;
- maximum teardown duration before quarantine;
- maximum copied diagnostic records.

No hub path may trigger unbounded recursion, allocation, event growth, descriptor buffering, retries, or waits.

## 24. Compatibility requirements

Existing direct-attached USB devices SHALL continue to work after hub support is added.

Existing class providers that use the current compatible host ABI SHALL continue to function when the host implementation becomes hub-capable.

A device behind a hub SHALL not require a class-specific firmware build.

Removing the hub-capable controller/host provider SHALL remove the corresponding USB functionality; compiled firmware SHALL NOT take over.

Adding support for a new hub chipset that is compliant with the underlying supported hub class SHOULD NOT require generic core modification.

## 25. Current implementation gap

At the time this specification was introduced, the U1 USB stack already had important foundations:

- `usb_controller_esp32s3` owns the physical ESP-IDF USB host stack;
- `usb_host_v2` owns generation-qualified logical device tokens and interface claims;
- class ELFs own CDC/CP210x/CH34x behavior;
- the host has bounded device and claim tables;
- generic semantic publication work is in progress.

The current controller implementation is not yet a complete hub/multi-device implementation because it uses a single controller-wide transfer object and controller-wide `inFlight/completed` state, while host events do not yet expose parent/port topology.

This section records implementation state only. The target requirements above govern future changes.

## 26. Implementation sequence

Implement hub support in this dependency order:

1. verify/upgrade the ESP32-S3 USB host backend to a version with supported external-hub behavior;
2. enable external-hub support inside the controller provider build/profile only;
3. preserve current direct-attached behavior and v1 ABI prefix;
4. add provider-owned topology identity and parent/port tracking;
5. add bounded hub-aware event handling and subtree invalidation;
6. replace the permanent single-transfer bottleneck with bounded multi-transfer scheduling;
7. add explicit controller/host resource accounting and structured exhaustion errors;
8. verify class providers can create multiple simultaneous instances;
9. integrate hub topology with provider-originated semantic device publication;
10. add VBUS/power-budget enforcement and self-powered-hub qualification;
11. add simulated topology/concurrency/failure regressions;
12. perform owner hardware qualification;
13. only after single-level hub acceptance, consider nested hub support.

## 27. Required simulated tests

Before hardware qualification, tests SHALL cover at minimum:

1. direct-attached device regression;
2. one hub plus two downstream devices;
3. one hub plus three different classes;
4. two devices of the same class;
5. keyboard + gamepad + serial active concurrently;
6. composite device behind a hub;
7. child detach while a sibling transfer remains active;
8. child reconnect to same port yields a new generation;
9. child moved to another port yields a new generation;
10. hub removal invalidates all descendants;
11. sibling device remains active after unrelated child removal;
12. valid detach cascade does not create a false controller fault;
13. event queue overflow fails explicitly;
14. device-slot exhaustion is explicit and bounded;
15. claim exhaustion is explicit and bounded;
16. host-channel/resource exhaustion preserves already active devices;
17. transfer-pool exhaustion applies controlled backpressure/failure;
18. one slow serial read does not indefinitely block HID progress;
19. failed child release quarantines only affected state where possible;
20. provider shutdown drains all descendant transfers before unload;
21. stale child token cannot access a replacement device;
22. no persistent callback can target an unloaded ELF.

## 28. Hardware acceptance sequence

Hardware qualification SHOULD proceed incrementally:

```text
Stage 1: self-powered hub + keyboard
Stage 2: self-powered hub + keyboard + gamepad
Stage 3: self-powered hub + keyboard + USB serial
Stage 4: self-powered hub + keyboard + gamepad + USB serial
Stage 5: repeated per-port hotplug/replug
Stage 6: remove entire hub during active HID + serial traffic
Stage 7: two devices exposing the same semantic capability
Stage 8: composite device behind the hub
Stage 9: controlled resource exhaustion
Stage 10: bus-powered hub only after power-budget qualification
```

Qualification SHALL record the hub make/model or controller identity, power arrangement, attached devices, provider versions, firmware version, observed topology, resource counters, failures, and teardown behavior.

## 29. Acceptance criteria

USB hub support is implemented only when all of the following are true:

1. one supported external hub can enumerate multiple downstream devices simultaneously;
2. at least three useful simultaneous semantic devices can be active where hardware resources permit;
3. HID input remains responsive while bulk serial traffic is active;
4. multiple instances of one semantic class remain independently addressable;
5. child unplug/replug uses fresh generations;
6. whole-hub removal cleanly revokes the downstream subtree;
7. unrelated siblings remain functional when one child fails;
8. resource exhaustion is explicit, bounded, and does not masquerade as detach;
9. power denial/overcurrent paths fail safely;
10. no unbounded transfer/event/buffer allocation exists;
11. provider unload waits for transfer/callback/DMA quiescence;
12. generic RiscRTE core contains no new hub-specific ownership, enumeration, descriptor parsing, power/reset, or transfer code;
13. class providers work unchanged or through a compatible ABI extension for direct and hub-attached devices;
14. hardware qualification succeeds on at least one documented supported hub/device matrix.

## 30. Failure criteria

The implementation is noncompliant if any of these occur:

- hub enumeration is implemented in compiled generic firmware;
- a special core USB-hub manager is introduced;
- applications need hub-specific code for ordinary keyboard/gamepad/serial use;
- the controller can enumerate multiple children but serializes all sustained I/O through an unavoidable single long-blocking transfer path;
- one child failure unnecessarily tears down healthy siblings;
- hub removal leaves descendant callbacks, DMA, claims, or streams live;
- power limits are ignored because the hub enumerated successfully;
- resource exhaustion silently drops devices;
- a stale device token accesses a reattached child;
- removal of the USB provider leaves a hidden firmware hub/USB fallback active.

## 31. Normative summary

1. **USB hubs are provider-owned hardware/protocol infrastructure, never a RiscRTE core subsystem.**
2. **Applications consume semantic capabilities and remain hub-agnostic.**
3. **The first required implementation supports one external hub level; nested hubs are a later bounded extension.**
4. **Every child device has independent generation-safe identity, claims, streams, and teardown.**
5. **Hub removal invalidates the full descendant subtree without disturbing unrelated devices.**
6. **Multi-device support requires bounded concurrent transfer progress, not enumeration alone.**
7. **Controller channels, claims, transfers, memory, events, bandwidth, and power are finite resources with explicit accounting.**
8. **Resource exhaustion never masquerades as detach and never causes unbounded allocation.**
9. **Power and VBUS safety remain owned by USB/power providers.**
10. **Existing direct-attached devices and class ABI behavior remain compatible.**
11. **All callbacks, DMA, claims, and transfers must quiesce before provider unload.**
12. **No compiled firmware USB fallback is permitted.**
