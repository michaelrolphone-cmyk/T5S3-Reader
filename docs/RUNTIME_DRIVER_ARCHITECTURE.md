# RiscRTE Runtime-Loaded Driver and Capability Architecture

## Status and authority

**Normative target specification.** Governed by [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), and the [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md). The hardware boundary is binding: the generic framework cannot know what USB, GNSS, I2C, SPI, a power controller, or any other hardware family is beyond seeing an opaque capability identifier. [Runtime Driver Implementation](RUNTIME_DRIVER_IMPLEMENTATION.md) records incompatible historical code without making it normative.

## 1. Objective and hard invariant

Applications request abstract capabilities. Independently installable ELF providers implement the capabilities. Providers may consume other providers' capabilities. The RiscRTE core supplies *generic* ELF loading, manifest/dependency resolution, execution contexts, permission mediation, event/stream infrastructure, and lifecycle dispatch; it MUST NOT contain operational device or transport drivers, type-specific managers, hard-coded hardware discovery, or USB/GPS-specific provider selection.

An ELF hardware driver SHALL implement actual device communication, state, discovery/probe as applicable, initialization, protocol, transfers, IRQ/callback handling, power and failure recovery. A lower-level controller or bus driver may implement shared functionality and publish its own capability, which another ELF consumes. Physical hardware ownership and arbitration belong to these providers. A generic execution-context lease is permission/lifetime accounting, not possession of the device by firmware. A thin proxy into a compiled-in hardware service is not a compliant driver.

A board-specific bootstrap/port implementation may boot the MCU, initialize memory/loader and minimal module storage. Recovery services may use separate explicit bootstrap paths; they MUST NOT register as silent production hardware fallback for absent or corrupt ELFs. The normal core and its dependency resolver remain unchanged when a new device/transport is installed.

## 2. Goals and non-goals

The design SHALL support independently installable/replaceable device, service, composite and filesystem providers; portable applications; versioned capability APIs; manifest-driven required/optional dependencies; cycle detection; explicit installed/active states; generic device observations; execution-context authorization and cleanup; logical aliases; stream backpressure; common VFS/mounts; removable and remote storage; independently provisioned hardware configurations; and driverless recovery. Adding a device or USB chipset SHALL require installing an ELF/profile, not rebuilding RiscRTE.

The design does not require full Linux Device Tree compatibility, POSIX process isolation, one ELF per physical chip, or removal of all CPU bootstrap functionality. It does not justify implementing device behavior in the core. A generic network service may live above provider capabilities; an IP/Wi-Fi controller or protocol implementation that is hardware-dependent belongs in a provider. Security mechanisms do not become physical hardware ownership.

## 3. Layering

```text
application ELFs
  -> semantic capability API / aliases / streams
  -> hardware-blind core: generic resolver, contexts, permissions, handles
  -> driver/provider ELFs (physical, virtual, composite, controller, bus)
       -> dependencies on capabilities from other ELFs
       -> minimal platform port primitives where essential
       -> hardware
```

The core considers `usb` just another dynamically declared name/version pair. It does not know whether it is a transport, device, controller, or abstract service. SDK interface definitions may specify USB operations, but only USB provider ELFs implement them. USB-specific SDK headers are not evidence that the compiled core may implement USB behavior.

For instance:

```text
serial_monitor.elf -> serial.port
usb_cdc.elf -> requires usb.host; provides serial.port
usb_host_controller.elf -> requires optional board.power.vbus; provides usb.host
board_power.elf -> provides board.power.vbus
```

Only the loaded provider ELFs interpret USB descriptors, controller registers, power chip protocols, endpoint configurations and retries. `serial.port` could instead come from a UART/BLE/network ELF with no change to the core or Serial Monitor.

## 4. Module classes

- **Application ELF:** user-facing workflows, UI and semantic capability consumption; no direct board/transport dependency when a capability exists.
- **Physical driver ELF:** full working hardware implementation for a device, controller, bus or transport. May provide low-level and high-level capabilities.
- **Composite/virtual provider ELF:** consumes capability APIs and exposes aggregation, fusion, codecs, protocols, filesystems or derived capabilities; need not control a physical device.
- **Service ELF:** independently managed background behavior consuming/provider capabilities, events and jobs.
- **Core/runtime:** generic loading, verification, resolution, execution and data movement, not transport implementation.
- **Platform bootstrap/port:** clearly isolated boot and generic CPU/RTOS primitive backend; not a place to hide production devices.

Every module class has a manifest, independently versioned ABI, documented lifecycle, security and resource limits.

## 5. Capability contracts

A capability is an arbitrary identifier, independently versioned API, provider identity, optional logical instance/binding, permissions, and metadata. Examples: `display`, `input.keyboard`, `storage.block`, `storage.filesystem`, `network.interface`, `serial.port`, `usb`, `usb.host`, `position.gnss`, `location.position`, `motion.accelerometer`, `radio.lora`, `battery.status`, `power.control`, `rtc`. Examples are *data*, never an exhaustive enum in core.

Applications SHOULD require `display.primary`, `position.absolute` or `network.default`, not a concrete display/GNSS/network driver. A logical alias maps to a chosen advertised instance and may change with profile/user selection. Selection precedence SHOULD be explicit binding, board/profile recommendation, then provider priority, subject to version/availability/permission checks. Future policy may consider reported power/quality/latency without giving the core protocol-specific behavior.

Capability loss or provider removal invalidates generation-safe consumer handles, dependent leases and affected streams; consumers receive bounded structured events and degrade or terminate according to their requirements.

## 6. Application and driver manifests

Application manifests SHALL list mandatory requirements and MAY list optional capabilities with compatible API versions. Missing mandatory dependencies block launch **before ELF mapping**; optional dependencies do not block launch. Example:

```json
{"type":"app","id":"maps","runtime_abi":2,"requires":[{"capability":"display.primary","api":2},{"capability":"position.absolute","api":1}],"optional":[{"capability":"network.default","api":1}]}
```

Drivers also declare requirements and provided capabilities. Example:

```json
{"type":"driver","id":"usb-serial-cdc","driver_abi":2,"requires":[{"capability":"usb.host","api":1}],"provides":[{"capability":"serial.port","api":1}]}
```

The manifest does not grant hardware access. Provider-side implementations MUST validate actual authorization on operations. Version negotiation is per API rather than a single firmware-wide capability ABI. Requirements may ultimately support alternative groups and detailed constraints, without adding special-case hardware code to the resolver.

## 7. Driver ABI and lifecycle

Expose a stable root symbol such as `t5_driver_get(requested_abi)` with `abi_version`, `struct_size`, module identity, generic lifecycle and capability registration/dispatch functions. The exact concrete ABI may evolve append-only; do not freeze current proxy-only `T5UsbClassDriver.h` as the general design. The ABI SHALL support hardware-provided probe, start, stop, suspend/resume, removal, capability registration/unregistration, provider-driven device publication, and complete/failed quiescence indication.

The runtime MAY copy bounded module metadata, verify signatures/integrity and serialize generic lifecycle calls. It MUST NOT call a special USB-specific driver activation path or parse a descriptor on the provider's behalf. Probe is called with authorized opaque resources or provider-supplied descriptors; only provider logic interprets them. Provider code and all borrowed buffers/handlers remain resident until in-flight calls, device I/O, callbacks and stream producers have stopped. Unload MUST NOT occur just because a generic lease refcount reached zero while hardware is still active.

Driver states SHALL distinguish `INSTALLED`, `LOADED`, `PROBED`, `BOUND`, `ACTIVE`, `SUSPENDED`, `FAILED`, `ABSENT`. Installed packages do not automatically bind, become active, or gain permission. Multiple providers may be installed and independently chosen for distinct device instances.

## 8. Generic dependency resolver

Resolve manifests into a directed graph, validate versions, detect/report cycles, load satisfiable dependencies, invoke generic provider lifecycle, accept provider-published capabilities on successful initialization, and repeat until stable. A failed requirement prevents its dependent module from starting. Dependency edges may connect physical, virtual, bus, storage, network, controller and fusion drivers equally. An ELF requiring `usb.host` is resolved exactly like one requiring `sensor.temperature`; no dedicated kernel USB manager participates. Selection must be driven by manifests, provider advertisements, constraints and policy.

Example fusion graph:

```text
ublox_gnss.elf -> position.gnss -----+
imu_a.elf -> motion.accelerometer ---+--> absolute_position_fusion.elf
imu_b.elf -> motion.gyroscope -------+        -> position.absolute
```

## 9. Hardware description and provider discovery

Board/device profiles are independently installable configuration data listing identities, compatible provider packages, resources, pins, addresses, rails, interrupts and default logical bindings. These descriptions are supplied to eligible provider ELFs and interpreted by those providers; the core sees opaque/bounded metadata and package constraints only. Board profiles must not compile hardware/device selection into the kernel. A provider detects physical hardware (or consumes another discovery provider), then publishes generic inventory/device records. No firmware-owned USB host polling, USB snapshot projection, or other transport-specific discovery logic is permitted in the target.

The generic registry validates event bounds and publisher/handle identity, tracks generation, state, leases and event sequence, and provides snapshots and gap detection. It never parses descriptors or claims hardware. Discovery is observation, not permission. A provider's class-specific device metadata is opaque to the core.

## 10. Hardware/resource ownership and safe sharing

**Provider ELFs own physical sessions and hardware state.** The core owns only generic policy decisions and bookkeeping for capability and execution-context grants. It may issue opaque resource grants via CPU/OS port primitives where the underlying resource must be globally exclusive; it MUST NOT know that a token represents a USB endpoint or implement SPI transaction ordering itself. When multiple drivers need a physical controller, the controller/bus provider ELF implements serialization and exports a versioned capability. For example, I2C address arbitration belongs to an I2C provider ELF; USB interfaces/endpoints belong to a USB-host provider ELF; a charger/rail driver supplies a power capability. Claiming via those dependencies prevents races without hardware knowledge in RiscRTE.

Privileged module access may be mediated by generic allocation, interrupt registration, scheduler, memory/DMA, synchronization, logging, watchdog and resource-token primitives, or by another provider's typed capability. Platform-specific ESP-IDF shims may implement generic CPU/OS primitives. An ESP-IDF USB host wrapper in firmware that actually performs USB operations is a forbidden hardware driver proxy, regardless of its API name. Provider-specific privilege must use package trust, policy and explicit authorization; native ELF loading does not itself confer isolation.

## 11. Storage and VFS

Apps see a logical root namespace (`/system`, `/apps`, `/drivers`, `/config`, `/home`, `/tmp`, `/mnt/...`) instead of SD-specific assumptions. Physical/block controllers, filesystem implementations and remote network filesystems can be distinct provider ELFs. A generic mount/namespace facility can maintain opaque mount lifetimes, names, rights, open-handle revocation and journaling without recognizing SD, USB MSC or WebDAV as hardware types. Storage-provider ELFs implement mount-specific recognition, file/block behavior and removal recovery. Generic file operations SHOULD include open/read/write/seek/close/stat/mkdir/remove/rename/opendir/readdir/closedir, with separately versioned extensions.

Bootstrapping the loader from a minimum built-in storage path is an explicitly isolated bootstrap concern; normal removable or network volumes are not compiled-in production drivers.

## 12. Networking

Applications consume network/HTTP/socket capabilities rather than a concrete Wi-Fi chip. Physical interface, radio management, link setup and device-specific recovery live in driver/provider ELFs. Generic networking, HTTP/TLS or shared protocol services may be separate service/provider modules or transport-independent platform services; do not place Wi-Fi hardware logic in the core. Provider interfaces may advertise optional scan, RSSI, security and link metadata. A recovery-only networking facility, if absolutely required, must not register as normal fallback capability or erase the distinction between bootstrapping and installed providers.

## 13. Composite services, displays, power and jobs

Providers may implement sensor fusion, filesystems, image codecs, audio mixers, screen transforms, protocol stacks and virtual devices. UI/rendering consumes a display capability whose driver ELF owns actual display transfers and refresh. Display metadata may describe size, pixel format, orientation, partial refresh and timing without core driver code. Hardware-dependent sleep/wake/power control belongs to power providers; the core may express policy and invoke generic capabilities. Generic executor, job/event and stream mechanisms own software scheduling, cancellation, copied events and bounded buffering, never device transactions.

## 14. Bootstrap and recovery

Stage boot: ROM/MCU, minimal port + executable memory + module-store access, generic runtime start, manifest inventory, dependency graph, provider activation, logical bindings, services/mounts, then shell/home app. On failure the device MUST offer an explicit bootstrap/recovery mode capable of installing packages without relying on the broken normal driver graph. The minimal recovery console/network/storage implementation may be board-specific if necessary, but is isolated from the normal RiscRTE core/provider namespace. **Do not allow compiled-in recovery Wi-Fi, USB, GPS, display or serial implementations to silently register as production providers.** A target with a recovery transport requiring hardware implementation documents the separate recovery image/port explicitly. Recovery-first-boot guarantees are tested separately from modular-driver functionality.

## 15. Package and security model

A driver package contains `manifest.json`, executable ELF, integrity metadata and eventually signatures, optionally configuration/profile/schema documentation. Installation, validation, loading, binding and activation are separate. The Package Manager supports offline removable-storage installation and online delivery with the same validated format, transactional upgrade/rollback/removal and quarantine. Privileged driver signatures/trust requirements SHOULD be stronger than app requirements. Hash validation is necessary but not sufficient for provenance. App consent, provider authorization, lifecycle cleanup and resource quotas are generic; driver-specific permission enforcement occurs in the provider. Do not claim native ELF memory isolation where absent.

## 16. Migration and acceptance

1. Implement generic capability ABI, resolver, registry, invocation authorization, lifetime, handle revocation, and ELF package system without type-specific branches.
2. Move *entire* USB host/controller, class/device, USB power, USB discovery, transfer and hotplug implementations out of firmware into provider ELFs, possibly decomposed into cooperating controller/class/power modules. Remove fixed `usb-cdc-acm` loader route and silent resident fallback from normal operation. Preserve legacy code only under labeled current-state docs until removed.
3. Apply the same end-to-end standard to GNSS, SPI/I2C/GPIO, display, radio, charging and networking. Move firmware-side device-specific publication and ownership logic into providers. Retain generic software contexts/leases and an explicitly separate boot path.
4. Implement an actual composite ELF consuming multiple provider capabilities and prove dependency acquisition/loss.
5. Move normal storage implementations into provider ELFs while maintaining VFS/boot viability; move network, display and input hardware likewise.
6. Remove remaining product-specific hardware from normal firmware and regression-test source/ABI boundaries.

**Mandatory proof:** install a driver for previously unsupported hardware with no firmware rebuild; verify real hardware I/O; uninstall it and observe capability disappearance without hidden native fallback; verify unplug/replug, concurrent resource conflicts, errors and shutdown. Host tests and successful firmware/ELF builds do not constitute physical acceptance. If an ELF only forwards into firmware hardware implementation, it fails this requirement.

## 17. Portability and completion invariants

The final device identity is minimal generic runtime + CPU/loader bootstrap port + independently installable provider set + board/profile data + logical bindings. Applications depend on capability APIs and rebuild only when CPU/ELF architecture requires it. The same resolver handles physical and virtual providers; API versions remain independent; missing mandatory capability prevents launch; installed does not imply active; provider failure revokes handles/events; root filesystem hides physical storage; recovery works without normal drivers; no transport-specific manager exists in the compiled RiscRTE core. Hardware drivers, not the framework, own the physical hardware.
