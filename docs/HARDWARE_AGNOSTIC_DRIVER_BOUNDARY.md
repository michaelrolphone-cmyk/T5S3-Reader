# RiscRTE Hardware-Agnostic Runtime and Driver Ownership Contract

## Authority and status

**Normative target architecture.** Read after [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and before any transport, board, peripheral, driver, registry, stream, or migration specification. This contract resolves conflicts in earlier documents that assigned USB, GPS, UART, BLE, SPI, I2C, power-controller, or other hardware implementation/ownership to the compiled RiscRTE framework. Those passages describe legacy implementation only and MUST NOT guide new work. See [RUNTIME_DRIVER_IMPLEMENTATION.md](RUNTIME_DRIVER_IMPLEMENTATION.md) for the actual migration state. No firmware or driver implementation is changed merely by adopting this specification.

## 1. Absolute boundary

**The RiscRTE core MUST NOT know what USB is.** Its only knowledge of `usb` is that it may appear as an opaque, dynamically registered capability identifier requested by software. This principle applies equally to GPS, UART, BLE, storage controllers, power-management chips, displays, and future hardware. The core MUST NOT contain transport-specific enumerators, protocol state machines, controller/endpoint/host handles, class selectors, VID/PID matching, device reset sequences, register maps, power sequencing, bus arbitration algorithms, recovery procedures, or hard-coded drivers. It MUST NOT create special-case control flow for a capability whose identifier happens to be `usb` or `serial.port`.

A driver ELF owns **the complete hardware implementation** for every capability it provides: device discovery/probing when applicable; device matching; controller initialization and teardown; bus/endpoint/interface management; protocol and register operations; data transfers; interrupts/callbacks; device power sequencing; hotplug and recovery; and hardware-related internal synchronization. An ELF may consume capabilities provided by other ELFs rather than duplicate subordinate implementations. For example, a USB class ELF can consume a USB-host capability provided by a USB-host-controller ELF. Both implementations remain outside the core. Driver authority is distinct from application authority: the generic runtime authorizes calls and manages contexts, while a driver implements the hardware-specific operations.

A driver that merely encodes a control packet, probes a descriptor supplied by firmware, or forwards into a compiled-in device-specific implementation **does not satisfy** the installable-driver requirement. Successful ELF packaging/loading is not proof of hardware modularity.

## 2. What remains in the core

The compiled RiscRTE core MAY implement only transport-neutral machinery: ELF loading/linking, version and manifest validation, package installation, generic dependency and capability resolution, generic invocation/execution contexts, opaque handles, rights/consent policies, lifecycle supervision, generic resource bookkeeping, memory and executable-memory facilities, scheduling, bounded streams/events, namespaces/VFS needed to load modules, and generic failure recovery. It interprets capability IDs as arbitrary versioned strings or opaque keys, with no built-in list of hardware families and no protocol-specific branches.

The core MAY allocate/revoke a generic capability lease and associate it with an execution context, but that lease does not imply that the core owns the underlying USB controller, UART, power rail, or device. The providing ELF owns its hardware implementation and actual hardware state. The core MUST NOT implicitly initialize, reset, claim, configure, power-cycle, or stop hardware on lease acquire/release; it invokes the provider's generic lifecycle/dispatch entry points and tracks the resulting opaque grants. The driver MUST perform its own device teardown when the runtime invokes generic cancellation/unload and MUST report completion or failure before its code is unmapped. A stalled driver requires a safe failure policy; native ELF unloading alone is not hardware reset or fault isolation.

A narrow **platform bootstrap/port layer** is unavoidable to boot a specific CPU, run the loader, configure memory, and obtain access to the module store. That layer is not a license to put ordinary hardware drivers into the RiscRTE core. Any boot-critical storage or recovery implementation MUST be explicitly labeled bootstrap-only, separated from normal runtime capability provision, and must not silently act as a substitute for an installed driver. Platform-specific system-call shims may permit driver ELFs to use SoC/RTOS primitives; they MUST NOT implement USB, GPS, serial adapters, or other device behavior on the ELF's behalf. Porting to another MCU replaces the bootstrap/ABI backend and relevant hardware drivers, not the generic resolver.

## 3. Capability graph, not a transport hierarchy in firmware

Capabilities are names and versioned interfaces published by providers, not compiled-in classes. A provider may provide `usb`, `usb.host`, `serial.port`, `location.position`, or any new identifier. The runtime does not derive type, privilege, or routing from the spelling. Manifest-declared requirements express dependencies; their meaning and operations are defined by the provider-facing capability ABI, not by core switch statements.

Example (a dependency graph assembled from manifests at runtime):

```text
serial_monitor.elf --requires--> serial.port
                                 ^
                                 | provides
usb_serial_cdc.elf --requires--> usb.host
                                 ^
                                 | provides
usb_host_controller.elf --requires--> platform controller access

usb_host_controller.elf --optional--> board.power.vbus
board_power.elf --------provides--> board.power.vbus
```

The core sees only IDs, dependency edges, versions, permissions, providers, handles, and lifecycle states. Only ELFs and board descriptions interpret USB addresses, endpoint descriptors, port roles, charger registers, or power requirements. A board description is input data for providers, not a source of compiled-in USB behavior. Another driver may provide `serial.port` using a completely different transport without any core modification.

**Adding a supported device or transport MUST NOT require a firmware source change, recompile, new built-in enum/branch, or new core-side provider bridge.** It may require a compatible kernel/port ABI version and a separately distributed ELF or data profile. New capability APIs can be independently versioned SDK contracts without teaching the resolver their protocol contents.

## 4. Discovery, registry, authorization and ownership

The Unified Device Registry is an optional **generic publication and observation service**, not a device manager that probes physical transports. Drivers publish/unpublish device and capability records through one transport-neutral ABI and supply copied/opaque identity and metadata. The core may validate record bounds, producer identity, generation, rights, sequence, and lease state, but MUST NOT parse USB descriptors, poll a USB host, synthesize USB inventory entries, or interpret vendor IDs. Discovery is performed by the driver or an independently installed discovery provider; events originate from provider publication and are delivered by generic journal/event infrastructure.

Generic execution-context leases grant scoped permission to use a provider's capability; they are not ownership of the physical device. The driver owns hardware sessions and mediates real resource contention. When multiple drivers need the same physical resource, they MUST acquire a common provider capability or an explicitly defined generic platform resource grant. They MUST NOT initialize or race a shared bus independently. Arbitration for a named hardware controller is implemented by its controller/provider ELF, not by transport-specific firmware code. If an OS/SoC primitive must enforce exclusive ownership, the platform layer may enforce an opaque resource token without knowledge of protocol semantics.

A driver-only privileged API MAY expose generic CPU/RTOS services (allocation, scheduling, interrupt registration, generic memory or opaque resource operations); a provider can also offer typed peripheral functionality as an ELF capability. Avoid hard-coded `kernel.usb.host` or similar hardware-specific kernel dependencies. A compatibility entry point with such a name MUST be labeled legacy and removed from the normal driver path during migration.

Drivers cannot be assumed memory-isolated merely because they are ELFs. Manifest validation, trust/signing, ABI limits, cancellation and resource accounting are necessary but do not themselves establish hardware isolation. State these limits explicitly rather than restoring compiled-in hardware ownership as a shortcut.

## 5. Driver ABI and lifecycle

Use one generic, versioned driver root entry point, manifest-based requires/provides, and provider-supplied capability interface tables or dispatch interfaces. A generic lifecycle supports `probe`, `start`, `suspend`, `resume`, `stop`, `remove` and failure publication as applicable; it does not expose type-specific core callbacks. Drivers register dynamically and publish only after functional initialization. The runtime checks API/ABI compatibility, creates contexts, resolves dependencies, authorizes consumers, dispatches via opaque handles, and unloads only after the provider has quiesced its hardware and no borrowed code/data/handles remain.

Provider and consumer roles can coexist in one ELF. A provider must not call into an ELF that has been unloaded. Handles and events must be generation-safe, and loss of a provider invalidates dependent leases/streams. Generic stream buffers and backpressure may live in the runtime, but the driver implements production/consumption and actual device I/O. Raw descriptor/control/transfer operations are defined by the installed USB provider ABI, never by a privileged core USB service.

## 6. USB migration acceptance example

The current `Drivers/usb_cdc/driver.c` implements descriptor matching and small request encoders; `src/native/NativeUsbBridge.cpp` still owns USB host initialization, power, handles, transfers and chipset-specific paths. `src/runtime/drivers/UsbCdcDriverRuntime.cpp` hard-codes a single USB ELF identity and activation route. These are **legacy noncompliant implementation facts**, not reusable architectural patterns. Other USB migration documents may describe their behavior for hardware regression tests, but cannot mandate that it remain in firmware.

Acceptance for a genuine USB conversion requires all of the following:

1. Normal firmware contains no operational USB host/class/chipset implementation, no USB-specific device projection or provider selection, and no required firmware-side USB bridge; only a transport-neutral capability resolver and any clearly isolated bootstrap primitives remain.
2. A USB host-controller/provider ELF owns host mode, OTG/VBUS policy (directly or via power-provider dependencies), enumeration, hub/topology, interface/endpoint claims, transfers and hotplug/recovery; USB class/device-function ELFs own their matching, protocols and requests. Functional decomposition across multiple ELFs is allowed; delegation back into firmware hardware code is not.
3. Install a previously unsupported USB class/chipset using only an ELF, its manifest and any board/profile data. Without rebuilding firmware, its capability becomes available and its real hardware I/O works.
4. Remove/disable the ELF and verify its capability disappears, handles/streams revoke, hardware quiesces, and no compiled-in implementation silently takes over. Recovery boot is tested separately.
5. Prove multiple providers, disconnect/reconnect, failed activation, cancellation, safe teardown, resource conflicts, and absent/corrupt package behavior. Build/host-test success is not physical acceptance.
6. Static checks reject new production firmware references to USB-specific headers, classes, enums, selectors, and protocol operations outside explicitly scoped platform bootstrap/legacy files being removed. Check other hardware families by the same principle.

## 7. Migration and documentation rules

Update [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), [PLATFORM_CAPABILITY_ROADMAP.md](PLATFORM_CAPABILITY_ROADMAP.md), [RUNTIME_DRIVER_ARCHITECTURE.md](RUNTIME_DRIVER_ARCHITECTURE.md), applicable device-specific architecture and SDK specs to this boundary. In documents preserving existing implementation, prepend target requirements before a **CURRENT/LEGACY, NONCOMPLIANT** section; do not rewrite history or claim an unimplemented migration is complete. No new spec may say the framework owns USB, the runtime USB host is canonical, drivers must avoid hardware implementation, or the driver ELF is only a class/protocol proxy. In new PRs, introduce no additional hardware-specific firmware services; move an entire functional vertical slice into independently loadable provider ELFs instead.

**Review gate:** If deletion of a driver ELF leaves that hardware working normally through firmware code, the hardware was not successfully modularized. If adding a novel device still requires modifying RiscRTE firmware, the core is not hardware agnostic.