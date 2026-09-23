# RiscRTE Platform Capability Roadmap

## Status and authority

This roadmap governs the progression from installed apps and limited provider slices to a general embedded runtime whose applications, services, drivers, protocols, devices and data sources can be independently installed. Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) first. **All hardware-related milestones are constrained by [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md).** Legacy claims that the framework owns USB, GPIO, I2C, SPI, UART, controllers, power rails, device discovery or physical resource management are superseded. They are migration-state descriptions, not target requirements.

> **Core invariant:** The RiscRTE core is hardware agnostic: `usb` and every other capability name are opaque, dynamically registered identifiers. Installable provider ELFs implement and own physical hardware, including controller/bus sharing, power, discovery, transfers, protocols and recovery. RiscRTE implements generic manifests, capability/dependency resolution, execution contexts, rights, generic events/streams, provider lifecycle and package loading. Adding hardware requires an ELF/profile, never a core firmware change.

## 1. Objective

```text
Applications / services
       -> capabilities / intents / jobs / generic streams
       -> hardware-blind core: resolver, contexts, generic registry
       -> independently installed physical/composite/provider ELFs
       -> provider dependencies / generic platform port primitives
       -> hardware
```

Applications describe *what* they require rather than *which* hardware implements it. Physical hardware must not be implemented in applications **or compiled into the core**. Generic framework-held authorization/lease records do not imply core ownership of physical devices. Driver ELFs own real hardware state and coordinate shared resources through controller/bus/power-provider capabilities.

## 2. Existing foundations and authority

This roadmap connects `RUNTIME_DRIVER_ARCHITECTURE.md`, `RUNTIME_DRIVER_IMPLEMENTATION.md` (current-state only), `SCENE_RUNTIME_ARCHITECTURE.md`, `SERVICE_RUNTIME_ARCHITECTURE.md`, `MEMORY_ARCHITECTURE.md`, `SECURITY_ARCHITECTURE.md`, `USB_OTG_HOST_ARCHITECTURE.md`, `PROGRAMMER_DEBUGGER_ARCHITECTURE.md`, `BLUETOOTH_SENSOR_ARCHITECTURE.md` and `PLATFORM_ABSTRACTION_ARCHITECTURE.md`. The hardware boundary overrides contrary historical wording. `HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md` is the required implementation/review gate for every hardware-related priority below. A new device capability must be introduced by installing a provider, not adding a transport branch in firmware.

# Part I — Core Platform Unification

## 3. Unified Device and Peripheral Registry

RiscRTE SHALL supply one **generic provider-publication registry** for physical and logical devices. It MUST NOT itself discover USB/BLE/UART/I2C/SPI/GPIO/LoRa/Wi-Fi devices, know which transport a string represents, parse descriptors, or maintain transport-specific subregistries. Installed provider ELFs observe their own hardware and publish copied/opaque identity, capability lists and lifecycle records through a transport-neutral ABI. A physical device may publish multiple semantic capabilities (temperature/humidity/battery, debug/programming, position/altitude/time). A provider is responsible for interpreting any transport-specific metadata it publishes.

### 3.1 Device records

Generic fields: opaque generation-qualified device ID, provider ID, optional opaque transport/identity strings, friendly name, availability/binding, capability IDs and API versions, generic grant/rights state, trust metadata, last observation time, and bounded provider-owned metadata reference. The runtime never interprets VID/PID, protocol class, UART address, I2C register or a transport identity as a privileged hardware fact.

### 3.2 Device lifecycle

Generic published states may include `DISCOVERED`, `IDENTIFIED`, `BOUND`, `AVAILABLE`, `BUSY`, `SUSPENDED`, `UNAVAILABLE`, `REMOVED`, and `FAILED`. The provider performs real probe/binding and hardware teardown; the core stores bounded state and invalidates software handles.

### 3.3 Device events

`device.discovered`, `device.available`, `device.unavailable`, `device.capability_added`, `device.capability_removed`, `device.identity_changed` and `device.error` are **generic event topics** emitted through provider publication. Support bounded journals, per-reader cursors, overflow GAP, mandatory resnapshot and generation-safe revocation; no firmware USB discovery tick.

## 4. Capability Resolver Expansion

The resolver is the implementation-independent indirection between consumers and providers. Capability examples (not built-in enums): `location.position`, `sensor.temperature`, `serial.port`, `usb`, `usb.host`, `storage.removable`, `input.keyboard`, `network.internet`, `debug.swd`, `program.target`, `notification.post`, `crypto.hash`, `credential.read`. Requests may specify device, minimum rate, preferred source, rights or exclusivity without naming a concrete driver. Resolve arbitrary manifest-provided names, API versions and dependency constraints to opaque grants; the driver mediates real access. Detect/report cycles and failure, and permit provider composition without special cases for hardware family names.

## 5. Unified Resource Ownership

Execution contexts own generic leases, stream handles, files, volumes, connection capability handles, timers, jobs, UI surfaces, memory mappings, subscriptions, credentials and software power-policy requests, with deterministic cleanup. Physical USB interfaces, BLE radio sessions, GPIO pins, I2C addresses, power rails or DMA device state are **owned/arbitrated by provider ELFs** and released by their provider lifecycle, not managed as hardware types by the framework. The core may manage an opaque platform resource grant without interpreting its hardware purpose. This distinction is prerequisite to safe ELF unloading; capability refcount alone is not proof hardware is quiescent.

# Part II — Hardware and Sensor Expansion

## 6. Generic Sensor Framework

A semantic measurement ABI SHALL be transport-independent. Provider ELFs for BLE, USB, I2C, SPI, UART, LoRa, Wi-Fi, internal ADC/SoC and GNSS may publish versioned samples without code paths in firmware for those sources. Example capabilities: temperature, humidity, pressure, acceleration, angular velocity, magnetic field, light, distance, voltage, current, power, battery, air quality and position. Calibration, unit conversion, sampling policy, channel metadata, latest-value cache, historical recording, quality flags, time synchronization, aggregation/downsampling and threshold detection should be generic services or independently installed providers, not hardware-specific core facilities. Apps consume a common measurement representation.

## 7. I2C, SPI and GPIO Provider Framework

**Corrected ownership:** I2C/SPI/GPIO bus and pin implementations MUST be ELF providers, not framework-owned bus managers. A bus/controller ELF implements enumeration/configuration, address/chip-select ownership, bounded transactions, serialization, timeouts, interrupt handling, DMA-safe buffers, arbitration, stuck-bus recovery and physical cleanup as applicable. A GPIO provider ELF owns reservation, modes, interrupts, pulls and safe release state. A sensor ELF requires the appropriate provider capability and profile-granted pins/buses/addresses; it never independently races another driver to initialize shared hardware. RiscRTE only resolves names, checks generic permissions, passes opaque grants, and invokes lifecycles. The platform bootstrap can expose generic OS/CPU primitives but cannot act as a compiled-in I2C/SPI/GPIO driver for normal runtime use.

```text
sensor_app.elf -> sensor.temperature
                         ^
                    sensor.elf -> requires i2c.bus
                                               ^
                                          i2c.elf -> hardware
```

Acceptance: install a new bus-connected sensor and any required bus ELF/profile without rebuilding firmware; concurrent clients and power/reset conflicts are enforced by the provider, not a core hardware-specific manager.

## 8. Location Framework

GNSS becomes semantic `location.position`, `location.altitude`, `location.velocity`, `location.heading`, `location.time`, `location.accuracy` and `location.satellites`, with source metadata and accuracy/quality indicators. A consumer is independent of internal/UART/USB/BLE GNSS or fusion. GNSS and underlying UART/USB/power hardware implementations and their discovery/polling remain in provider ELFs. Firmware `GpsDriverRuntime` or fixed facade paths are current-state migration targets, not the location framework architecture.

## 9. Observation and Sensor Fusion

Timestamped GNSS, IMU, BLE sensor, battery, LoRa and future observations use a generic temporal schema with validity, quality and provenance. Recorder, app and fusion providers consume the same streams. Fusion estimators are replaceable ELFs requiring lower-level capabilities; the core may supply time/stream mechanisms but never embeds the sensor fusion or hardware-reading algorithm. Initial objective: reliable temporal alignment, not a universal estimator.

# Part III — Streams, IPC, and Data Movement

## 10. Stream and Pipe Abstraction

Generic bounded byte/record streams support serial-to-terminal/file, GNSS-to-recorder, BLE sensor-to-recorder, LoRa-to-decoder, file-to-programmer, network-to-updater and sensor-to-graph use cases. Metadata includes read/write, type, seekability, source/sink identity, buffer/backpressure policy, EOF and disconnect. Pipes connect compatible endpoints (`source -> transform -> sink`); transform ELFs may decode, encode, filter, compress, checksum, log, rate-limit or packetize. Every stream has bounded buffering and explicit overflow/backpressure. **The runtime supplies generic buffers/routing; the installed driver performs physical RX/TX.** A firmware USB-specific open/read/write stream is legacy.

## 11. Inter-Module IPC

Use typed/versioned event publish/subscribe, request/reply, one-way messages, streams/pipes, job completion and generic capability invocation instead of persistent cross-ELF callbacks. Durable structures never retain an unloaded module's pointer. Rights and message bounds are enforced without inspecting protocol payloads.

## 12. Unified Event Bus

Expand generic events for `system.*`, `power.*`, `network.*`, `storage.*`, `device.*`, `sensor.*`, `location.*`, `usb.*`, `bluetooth.*`, `application.*`, `package.*`, `job.*`, `notification.*`. These are optional provider-chosen **topic strings**, not hardware-specific core event families or parsers. Define bounded delivery, sequence/overflow and durability appropriate to event class.

# Part IV — Storage, Files, and Data

## 13. Unified Storage Provider Layer

SD, USB MSC, internal flash and network volumes expose a shared logical volume ABI; provider ELFs own media-specific controllers and filesystem/protocol behavior. Generic volume metadata: ID, provider/device, mount point, filesystem type, capacity/free space, read/write, removability, generic ownership and health. Publish volume-added, mounted, unmounting, unmounted, removed, low-space and error events. Safe removal coordinates open handles, mappings, recorder buffers, jobs, export clients and filesystem flush through providers; the generic mount/namespace manager does not implement USB MSC or SDMMC.

## 14. Unified Data Recorder and Time-Series Store

Generalize BLE recording to sensors, GNSS, battery, LoRa packets, USB instruments, runtime metrics, network and application-defined channels. Provide channel registration, append/batch, query by time, latest sample, aggregation, export and retention. Require bounded buffers, recoverable/versioned formats, timestamp validity, storage quota, low-priority interruptible indexing and batching instead of a filesystem transaction per sample. Consume streams from installed providers; no special USB/GNSS core readers.

## 15. Content Type and Intent Routing

Route image/png, image/jpeg, EPUB, Markdown, platform packages and firmware resources to manifest-registered handlers rather than hard-coded extensions. Generic operations include open, open-with, share, inspect, install and program; inspect handler manifests without loading candidate app ELFs.

## 16. Clipboard and Share Framework

Use bounded typed text, resource reference, handle, measurement, location and small binary payloads; share large files by reference/authorized handles rather than memory copy.

## 17. System Search and Indexing

Index files, documents/books, apps, settings, sensor-channel metadata/history, logs and docs through an incremental, storage-aware, interruptible low-priority service with bounded active cache and persistent index.

# Part V — Networking and Communications

## 18. Networking Services Above Wi-Fi

Physical Wi-Fi/radio/link management is implemented by provider ELFs, not compiled into firmware. Transport-independent DNS, HTTP(S), WebSocket, NTP, mDNS, resumable download, connection events, TLS policy and future MQTT are reusable service/provider capabilities above an installed network interface. Applications do not replicate connection lifecycle, TLS policy, retry or download code. The existing fixed compiled Esp32NetworkProvider is **legacy noncompliant**, except isolated bootstrap recovery that is not a silent production fallback.

## 19. Unified Communications and Message Transport

Provide message/packet endpoint/routing semantics over BLE, LoRa, Wi-Fi/IP, USB CDC, UART and later providers without making the core understand those transports. Message -> endpoint/policy -> installed transport provider -> physical link. Start with common endpoint semantics; add failover/mesh later.

## 20. Device and Service Discovery

[Firmware Input Navigation](FIRMWARE_INPUT_NAVIGATION.md) describes the installed
`input.navigation` consumer path and foreground app handoff. USB class mapping
stays in the composite ELF; the firmware handles generic navigation and lifetime.

BLE advertisements, USB enumeration, mDNS, known UART/I2C devices and LoRa peers are discovered **inside independently installed providers**. Those providers publish generic observations to one registry; the core merely stores copied records and reports events. It MUST NOT enumerate these transports itself. A weather station may publish sensor capabilities, a PC network/debug capabilities, a probe SWD, and a GNSS receiver location, without a built-in transport-specific registry path.

# Part VI — System Services and User-Facing Facilities

## 21. Notification Framework

Apps/services post through a generic notification capability, not global UI ownership. Records may include source, priority (background/normal/important/critical), title, message, icon, timestamp, persistence, actions and wake/display policy. Actions route via intents/messages, never stale pointers.

## 22. Persistent Alarm and Deadline Framework

Provide exact, flexible, periodic, idle, monotonic and local-civil-time deadline classes. Scheduling metadata survives service unload and optionally deep sleep/restart. Generic scheduler policy may call an installed wake/power capability; MCU-specific wake-source programming is a provider/port responsibility, not hardware-specific core logic.

## 23. Automation and Rules Engine

Declarative event/capability rules may trigger recorder marks, notifications, jobs and installs, without requiring foreground apps. Rules require explicit permissions, bounded execution, rate limiting, loop detection, quotas and auditing. Device events originate from providers, not core transport polling.

# Part VII — Security Services

## 24. Credential and Secrets Vault

Credentials are policy-controlled opaque objects for Wi-Fi, BLE bonds, APIs, TLS/service credentials and SSH, with authorized-use handles instead of plaintext exposure where feasible. Provider ELFs may request granted credentials, but generic policy does not become hardware ownership.

## 25. Cryptographic Service

Version capabilities for random, SHA-256, HMAC, signature verify/sign with protected key, certificate verification and key derivation. Distinguish authentication, integrity, encryption and hash. Keys use opaque handles where possible; hardware secure-element drivers remain ELFs rather than core-specific integrations.

# Part VIII — Package and Module Management

## 26. Unified Package Model

A package contains manifest, one or more ELF modules, resources, schemas, dependency/capability declarations, CPU/ABI compatibility, platform/security version and integrity/signatures. Types: application, driver, service, provider. Package is distribution unit; ELF is loadable implementation. Driver packages MUST include **functional hardware code**, not only a proxy into firmware.

## 27. Package Manager

Manage install, validation, signatures, dependency/version checks, update, atomic replacement, rollback, uninstall, quarantine and inventory without executing unverified manifests. Offline removable-storage installation MUST work without requiring desktop-side Python. Online App Store/update delivery consumes the same format. Never silently substitute a firmware hardware driver for a missing/broken installed ELF.

## 28. Dependency Model

Prefer capability and ABI dependencies, e.g. `bluetooth.gatt.client >= 1`, `sensor.framework >= 1`, `usb.host >= 1`, over ELF filenames. Concrete provider dependencies may exist when unavoidable. Dependency graph is generic and rejects cycles; consumers of low-level transport/bus capabilities coordinate through provider ELFs, never a new built-in controller manager.

# Part IX — Jobs and Long-Running Work

## 29. Generic Job Framework

Reusable jobs include programming, file copy/move, download, package install, index rebuild, sensor export, book conversion, storage verification and sync. States: queued, waiting, running, paused, cancelling, complete, failed, cancelled. Metadata: job ID/type, owner, progress/status, times, **generic** capability grants, cancellation, resumability and result/error. System job UI presents active jobs. Programming protocol and target hardware work are performed by ELF providers; job core schedules and tracks only.

# Part X — Diagnostics and Observability

## 30. Structured Logging

Bounded timestamped/severity/module/context/event logs with structured fields, serial diagnostics and optional persistent batches, across core/ELFs/services.

## 31. Runtime Observability

Expose generic memory usage, loaded ELFs, module resources, tasks/work, capability grants, power-policy requests, files/mounts, streams, jobs, service/provider state, crash/watchdog history and queue overflow. USB topology, BLE devices, sensor registry or network state can be displayed **only as provider-published diagnostic data**; core MUST NOT query transport-specific firmware managers.

## 32. Crash and Failure Records

Capture module/context, versions, fault address/reason, generic resource snapshot, memory pressure and log tail. Quarantine optional failed providers while preserving boot/recovery; native ELF memory isolation is not assumed and quiescent hardware teardown must be established.

# Part XI — Integration

## 33. Application Model

Applications consume capabilities, intents, jobs, streams, events, handles and semantic devices. They do not implement USB, BLE, I2C/SPI, Wi-Fi hardware, filesystem global state, scheduler or system power. Equally, physical implementations do not move into RiscRTE core merely because apps stop owning them.

## 34. Service Model

Sensor Recorder, Search Indexer, Package Updater, Notification Manager, Time Synchronizer, Automation and Export/Sync are logical services with lifetimes independent of ELF residency. Hardware-dependent work goes through installed providers.

## 35. Driver Model

Driver ELFs implement complete working hardware and publish capabilities: GNSS/UART -> location, USB host plus FTDI/CDC -> serial, BLE radio -> bluetooth, I2C sensor -> sensor, USB MSC -> block/storage. Drivers exclude unrelated UI workflows but own required hardware protocols, transfer loops, control and recovery. No firmware-side driver proxy passes acceptance.

## 36. Provider Model

Physical, protocol and composite providers all participate in the same graph: BLE sensor decoder, Espressif/STM32 programming, GNSS/IMU fusion, stream codec, content parser. A protocol provider may consume serial/USB/etc. but its entire algorithm resides in an ELF, not in compiled firmware. The core treats them all as arbitrary capability implementers.

# Part XII — Priority and Implementation Order

## 37. Priority 1: Unified Device Registry and full provider publication

Retain generic generation-safe inventory/grants/events and eliminate hard-coded USB/GNSS firmware projection. First acceptance: installed USB, BLE and GNSS ELFs independently publish three logical devices via one registry, without transport-aware registry branches or a core USB discovery tick. Generic execution-context grants do not transfer hardware ownership from drivers to framework.

## 38. Priority 2: Streams and Pipes

Serial -> terminal/file, GNSS -> recorder and file -> programmer use the same bounded streams. Hardware I/O belongs to producing/consuming ELFs, not a firmware USB stream adapter. Reference app migration alone is not proof that hardware is modular.

## 39. Priority 3: Unified Package Manager

Install/upgrade/rollback/remove independently signed app/driver/service/provider packages with manifest/ELF validation and dependency graph. Acceptance includes genuine new hardware driver installation without rebuilding core and no hidden firmware fallback after uninstall.

## 40. Priority 4: Generic Sensor and Recorder

BLE temperature, GNSS and battery telemetry publish interoperable timestamped records; one recorder queries/exports synchronized history without embedded hardware readers.

## 41. Priority 5: Bus and Controller Provider ELFs

**Corrected priority:** deliver independently installed I2C/SPI/GPIO/UART/USB-host and other controller/bus provider ELFs, their versioned capabilities, and provider-owned arbitration. Do **not** implement framework-owned bus managers. Prove multiple downstream device ELFs share a provider safely and add a new device via packages/profile alone.

## 42. Priority 6: Jobs, Notifications and Intents

A File Browser selects firmware, an intent invokes an installed programming provider/job, generic job UI reports progress and a notification completes the workflow. Neither core nor app implements flashing hardware/protocol.

## 43. Priority 7: Vault and Crypto

Centralize permission-scoped secret use and cryptographic services before broadly distributing privileged third-party ELFs; strengthen driver package signatures and trust.

## 44. Priority 8: Search, Automation and Communications

Build on reliable generic device publication, events, sensors, jobs, streams and intents; keep transports in providers.

## Acceptance and migration rule

Preserve functional legacy current-state documentation separately in [RUNTIME_DRIVER_IMPLEMENTATION.md](RUNTIME_DRIVER_IMPLEMENTATION.md); do not label current compiled USB/GPS/network bridges compliant. The next hardware milestone MUST show actual ELF-owned I/O, installation without firmware change, ability to remove the provider without a resident substitute, safe shared-resource/hotplug behavior, generic registry publication and successful physical tests. **Any proposal placing hardware-specific ownership or implementation in the RiscRTE framework conflicts with this roadmap.**
