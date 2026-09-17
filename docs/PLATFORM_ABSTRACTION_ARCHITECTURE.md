# RiscRTE Platform Abstraction and Execution Runtime Architecture

## Status and authority

**Normative target.** Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md) and [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) first. This specification governs how applications/services avoid OS and hardware implementation details. Any historical text saying that the framework owns USB, GNSS, physical buses, a display panel, Wi-Fi radio, charger, device power transitions or concrete device I/O is superseded. The compiled core MUST know only generic capability identifiers; installed driver ELFs own physical hardware. [RUNTIME_DRIVER_IMPLEMENTATION.md](RUNTIME_DRIVER_IMPLEMENTATION.md) documents remaining incompatible code.

> **Invariant:** Feature code expresses intent, state, events, capability requirements and durable data. The generic RiscRTE runtime supplies execution, memory, policy, software resource grants, generic events/streams, data namespaces, scheduling and resolution. Driver/provider ELFs implement physical hardware, including controller/bus ownership, discovery, power, data transfers, refresh, and recovery. A core-held lease grants software authority, not ownership of a USB controller or other physical device.

## 1. Goal

Apps/services should not know CPU core, FreeRTOS tasks/queues, SRAM/PSRAM allocation, backing storage, transport identity, Wi-Fi radio mechanics, panel-refresh implementation, sleep hardware, ELF unloading or provider selection. Likewise, **the generic core must not know hardware protocols or types**. It resolves dynamically registered arbitrary capability names and communicates with installed providers through stable APIs.

```text
feature ELF -> state/actions/events/capabilities/data
        -> generic runtime (execution, contexts, policy, resolver, streams)
        -> installable driver/provider ELFs
        -> provider-to-provider dependencies and narrow CPU/OS port primitives
        -> hardware
```

A separate minimal bootstrap/port may bring up CPU/memory/loader/module storage; it must not hide ordinary production device implementations or silently act as driver fallback.

## 2. Architectural boundary

Normal app/service ELFs use abstract APIs. The core implements generic mechanics and does not import ESP-IDF USB/Wi-Fi/GPIO/charger/display drivers or switch on USB/GPS capability names. Physical driver ELFs may use an authorized low-level port API or another provider's capability. A protocol-specific wrapper in compiled firmware is still a firmware hardware driver even if called a kernel API and is prohibited for normal device support. No new device requires a firmware rebuild when the relevant driver ABI exists.

## 3. Module classes

Application/scene ELFs implement UI and user workflows; service ELFs implement managed background logic; hardware driver ELFs implement complete controller, bus, transport or device functionality; composite/provider ELFs transform capabilities into other capabilities; the generic core manages loading, rights, contexts and lifecycle. Modules have separately versioned manifests/ABIs/permissions. No class may be used as a pretext for a hardware-specific core manager.

## 4. Execution Runtime

Apps/services should use event-driven entry points (`on_appear`, `on_action`, `on_event`, `on_disappear`) and bounded asynchronous work rather than permanent direct FreeRTOS task ownership. The generic executor schedules, tracks and cancels software work; hardware-oriented provider ELFs implement their own device loops/callbacks under tracked lifecycle and must quiesce before unload.

## 5. Asynchronous jobs

Opaque job handles represent operations with started/progress/complete/failed/cancelled events. The core manages scheduling, status, cancellation and software grants, not the protocol implementation. `program.esp_rom` and any actual device programming live in a provider ELF, not inside a kernel job function.

## 6. Executor classes

Presentation/event, I/O, CPU/background, system and driver execution contexts may be scheduling classes; exact tasks, core affinity, stacks and queues are implementation details. Workload intent and owner identity are public; resource-specific hardware task ownership remains with its provider.

## 7. Unified event/message model

Use generic lifecycle (APPEAR/DISAPPEAR/SUSPEND/RESUME), input (ACTION/TOUCH/KEY), job (STARTED/PROGRESS/COMPLETE/FAILED/CANCELLED), system (network/time/storage/power/sleep/resume) and capability (AVAILABLE/CHANGED/LOST) notifications. Hardware-specific event topic strings can be published by ELFs as opaque data; core must not parse them into transport-specific behavior. Use bounded copied/versioned messages and opaque handles, not long-lived cross-ELF callback pointers.

## 8. Resource Context

Every app/scene/service/driver invocation has a generic context identifying software allocations, mappings, files, jobs, timers, subscriptions, capability grants, UI objects and provider handles. Destroying the context cancels/revokes these software resources and asks the provider to terminate hardware operations. **It does not mean the framework directly releases USB interfaces, powers a GPS rail or performs device reset.** The provider must report quiescence before unloading.

## 9. Opaque handles

Use typed owner/generation/rights-checked file, job, timer, capability, subscription, policy request and store handles. Provider hardware handles are private to the providing ELF or exchanged through its own versioned ABI. Generic core cannot interpret a USB endpoint handle or treat it as an internally owned endpoint.

## 10. Memory by intent

Feature code requests TRANSIENT, RESIDENT, LARGE, DMA, CACHE, PERSISTENT or VIRTUAL classes, not raw ESP allocation flags. Generic memory manager maps hot data to SRAM, resident/executable data to appropriate PSRAM/allocations, cold data to storage-backed windows, and DMA to compatible memory. Driver ELF must declare constraints and validate its own DMA/transfer buffer lifetimes; generic memory allocation does not confer device operation ownership.

## 11. Data Runtime

Namespaced preferences, durable data, documents, cache, temp and virtual objects separate mutable data from bundles. Generic runtime owns logical namespace, quotas and metadata. Physical filesystem/media I/O is implemented through installed storage/filesystem providers; a minimal module-store bootstrap path remains separately identified.

## 12. Storage-backed large data

Apps use logical handles and temporary mapped views over large collections, documents, images, indexes and downloads. Cache/window placement is generic; SD/USB/network device interaction remains provider-defined.

## 13. Transactions

Generic begin/commit/abort, atomic replacement and crash-recovery abstractions prevent each feature implementing its own backup/rename/journal conventions. Actual filesystem semantics are supplied by the storage provider capability.

## 14. Virtualized collections

The generic UI collection API offers count/item/action, visible-range, row reuse, selection/focus, pagination, prefetch, VM windowing and incremental invalidation, supporting small and very large lists. Rendering policy belongs to generic UI; **actual e-paper panel driving and physical full/partial refresh live in the display provider ELF**, not a framework-owned hardware renderer.

## 15. Capability Runtime

Capabilities are arbitrary name/version/metadata contracts registered by ELFs (examples: `position.gnss`, `battery.status`, `usb`, `serial.port`, `network.internet`, `time.clock`, `notification.post`, `search.index`). The core resolver is agnostic to names and whether a provider is physical, virtual or composite. Built-in normal hardware providers are forbidden; generic core software facilities and isolated bootstrap are not hardware driver substitutes.

## 16. Capability leases

A generic lease identifies an authorized consumer, provider, API/version, rights and lifetime. It keeps dependency/provider residency according to policy and invokes generic provider lifecycle. Physical controller ownership, contention, device power-down and safe shutdown remain provider responsibilities; the core MUST NOT power down hardware simply because its software reference count reaches zero. Loss invalidates handles and propagates generic events.

## 17. Service Runtime

Resident, event-driven, scheduled, periodic, on-demand, boot and idle services use managed lifecycle, dependency resolution, budgets, restart, persistence and unload. Time sync, updater, alarm and indexing are reference services. Hardware-specific implementation inside a service must instead be an appropriately privileged provider capability and cannot be hidden in compiled core.

## 18. Persistent scheduling

Schedule at/after/periodic/cancel APIs maintain deadlines independent of service residency. Generic scheduling decides deadlines and policy; a dedicated RTC/wake/power provider (or isolated minimal CPU bootstrap primitive) programs actual MCU hardware. The core should not contain device-specific wake-source code.

## 19. Power Runtime

The core may compute *generic policy* such as sleep eligibility, desired wake deadlines and owner-scoped power requirements, and clean leaked software policy requests. Hardware power modes, rails, charger registers, USB VBUS, radio state, panel power and actual sleep-entry wiring are implemented by installed power/board/controller providers. Apps/services request capabilities rather than controlling global state. A power policy enum must not become a USB-specific core power manager. If a provider fails to quiesce, follow an explicit safe failure policy instead of assuming a generic lease release powered hardware down.

## 20. Networking Runtime

Feature code requests HTTP, upload/download, availability and optional WebSocket/streaming; generic jobs can handle timeouts, cancellation and buffering. Installed networking providers own Wi-Fi/radio, link association, actual sockets/transport and associated physical power. Reusable DNS/TLS/HTTP components are transport-independent services/providers, never justification for compiled Wi-Fi control. Recovery-only networking is explicitly isolated.

## 21. Presentation Runtime

Generic UI owns view hierarchy, layout, navigation, Back behavior, lists/scrolling, focus/input routing and invalidation; an installed display provider owns panel commands, framebuffer transfer, ghosting/partial/full refresh constraints and physical commit policy. A display-specific policy may be supplied through provider metadata/ABI; normal core cannot hard-code e-paper hardware behavior. Touch hardware is likewise an ELF provider.

## 22. State-driven UI integration

Authoritative model/navigation state is independent of disposable controllers and visible UI. State changes update resident views incrementally; only the installed display provider drives actual pixels.

## 23. Cancellation

Operations surviving the caller require explicit cancellation and context-triggered cleanup. Jobs reach terminal status and software resources are released; hardware providers implement actual stop/abort and report safe quiescence before unload, without core device reset operations.

## 24. Timeouts and deadlines

Generic operations have deadlines/budgets and structured failure events. Provider-specific timeout/retry protocol is driver code; a generic watchdog cannot replace USB recovery or charger fault handling.

## 25. Fault containment

Represent stale handles, quotas, job/service timeouts, ELF load/ABI/provider/storage failure as APP_FAILED/SERVICE_FAILED/DRIVER_FAILED/JOB_FAILED/CAPABILITY_LOST. Native ELF is not process isolation: memory corruption may compromise system. Physical failure policy is provider-specific, not a core hardware recovery routine.

## 26. Resource budgets and quotas

Track SRAM/PSRAM, VM maps/cache, handles, jobs, CPU, timers, generic network grants, policy requests, wakeups and persistent storage per context. Apps/services/drivers may have different limits. Hardware transactions and actual resource arbitration belong to provider ELFs.

## 27. Hardware independence

An app requests `position.gnss`, not `UART1 RX GPIO44 at 9600 baud`. A generic core knows neither expression as hardware semantics; it resolves the capability string. Device-specific details reside in ELF implementations and optional board profiles. This must hold for every device and transport, not just application source.

## 28. Escape hatches

Advanced low-level capabilities (raw serial/socket/filesystem or bus access) require explicit provider-defined privileges and may reduce portability. Such interfaces are delivered by drivers, not special-case firmware hardware APIs.

## 29. Developer programming model

Applications comprise state, scenes, actions, events, capability requests, jobs and logical data; services comprise events/schedules, bounded work, capability requests, persistent state and outputs. Neither normally owns RTOS tasks, board initialization, sleep hardware or display driver. Hardware driver ELFs intentionally do own their corresponding hardware implementation.

## 30. Example feature flow

Weather scene -> acquire generic network capability -> request async refresh -> receive JOB_COMPLETE -> update model -> invalidate UI -> display provider physically commits. The app is unaware of tasks/buffers/Wi-Fi/panel timing, and the core is unaware of Wi-Fi/controller/panel hardware.

## 31. Core responsibilities

Core: minimal boot/loader, signature/integrity validation, scheduler/contexts/handle tables, generic event bus, persistent deadline metadata, memory manager, logical data namespace, arbitrary capability resolver, service/driver package managers, generic policy and crash/watchdog diagnostics. Optional bootstrap storage/recovery is isolated. **Not core:** any production hardware controller, bus manager, USB host/class, Wi-Fi radio manager, GPS UART, charger/rail manager, panel driver or hardware-specific power/sleep operation. Those are ELF providers.

## 32. Security/isolation

Public APIs validate buffers, types, owner, rights, lifecycle and generations; avoid raw cross-ELF callbacks, validate packages and support provider revocation/unload. Centralize *authorization* in the generic core and *hardware implementation* in trusted providers. Signed packages and stronger isolation remain necessary; putting hardware in firmware is not an acceptable shortcut.

## 33. Observability

Report contexts, resident modules, jobs, generic software grants, memory, VM pressure, provider capability state, services, deadlines, policy requests and failures. USB topology/transport details may be surfaced as opaque provider-published diagnostics; the core must not query a compiled USB manager.

## 34. Implementation sequence

1. Generic context/handles and deterministic software cleanup.
2. Execution/job/cancellation/event model.
3. Versioned generic events, provider publication and subscriptions.
4. Arbitrary capability names, manifests, resolver and rights; provider-managed hardware sessions/power.
5. Data runtime with namespaced storage, transactions and VM.
6. Persistent deadlines and service activation, with actual RTC/wake operations in providers.
7. Generic networking services with physical link/radio implementations in ELFs.
8. Generic UI/navigation/virtualization with actual display and input hardware implemented in ELFs.
9. Signed privileged drivers, stricter API rights/quotas, and containment.
10. Remove legacy firmware device-specific bridges/projections/managers; validate independent driver installation and physical teardown.

## 35. Acceptance criteria

Representative apps/services use no direct FreeRTOS task management, heap-tier selection, raw storage paths, Wi-Fi lifecycle, panel commands, power hardware locks or UART/SPI/I2C/GPIO calls for ordinary capabilities. **Additionally**, normal compiled firmware has no operational hardware implementation or transport-specific device managers. Install new hardware using only ELFs/profiles; uninstall its driver and verify no silent resident fallback; generic capability resolver/streams remain unchanged. Verify real hardware, resource cleanup and safe provider unload, not just compilation.

## 36. Architectural test applications

Maintain weather/network UI, large virtualized catalog, time-sync service, updater, alarm/deep-sleep service and GNSS consumer references. Add a driver installation test for a previously unsupported device and an absence-after-removal test. If any reference needs app-specific ESP-IDF access, it exposes a missing provider API; if the core gains a USB/GNSS hardware branch, it violates the architecture.

## 37. Final architecture

```text
Apps / scenes / services
     -> generic state / actions / events / data / capabilities
     -> RiscRTE core: execution / policy / generic handles / streams / resolver
     -> installed physical, virtual and composite provider ELFs
     -> other providers / narrow platform CPU+OS port
     -> hardware
```

**Feature software describes required outcomes; provider ELFs implement the hardware; RiscRTE only resolves and supervises the software contracts.**
