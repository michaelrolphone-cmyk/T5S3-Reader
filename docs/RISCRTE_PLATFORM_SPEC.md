# RiscRTE Platform Specification

## Status and authority

This is the **canonical entry point for RiscRTE architecture and specification work**.

All contributors and coding agents MUST read this document before making architectural changes, adding platform APIs, adding applications/services/drivers/providers, or changing resource ownership. New work MUST follow this specification tree and the [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md).

When documents disagree, use this precedence order:

1. this master specification and its invariants;
2. `PLATFORM_CAPABILITY_ROADMAP.md` for intended platform direction and sequencing;
3. the applicable current architecture specification listed below;
4. implementation/MVP documents for the currently implemented subset;
5. legacy implementation notes, retained only to describe code that has not migrated yet.

A legacy implementation description is not permission to extend the legacy design. New work should move toward the canonical architecture unless a compatibility requirement explicitly prevents it.

## Naming

The platform and runtime are named **RiscRTE (RISC Runtime Environment)**.

`T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware/board names, not platform names. Historical `T5*` source symbols, ABI structures, paths, build environments, release artifacts, or implementation identifiers may be named where necessary to identify existing code. They are compatibility identifiers and MUST NOT be used as the conceptual name of a new platform facility.

Likewise, documents historically using **native app**, **native API**, or board-specific terminology should describe the forward architecture as **RiscRTE application**, **RiscRTE host/platform API**, capability, provider, service, stream, or device as appropriate. Existing `native_*`/`t5_*` identifiers remain documented where changing the identifier would misdescribe the implementation or break compatibility.

## Core platform invariant

New platform functionality SHOULD normally be implemented as a reusable **capability, provider, service, stream, device, job, intent/content handler, or package**, not as private infrastructure embedded in one application.

Applications state what they require. The runtime resolves those requirements to implementations. Hardware access belongs below bounded runtime-owned abstractions. Cross-module communication uses versioned handles/messages/events/streams rather than persistent raw pointers across ELF lifetimes.

## Canonical architecture

```text
                         RiscRTE Applications
                                  |
                     intents / jobs / capabilities
                                  |
              +-------------------+-------------------+
              |                   |                   |
           Devices             Services              Data
              |                   |                   |
              +-------------+-----+-------------------+
                            |
                      Streams / Events
                            |
                     Capability Resolver
                            |
              +-------------+-------------+
              |             |             |
           Drivers       Providers      Runtime
              |             |             |
              +-------------+-------------+
                            |
                  Runtime-owned Hardware APIs
                            |
                         Hardware
```

The architectural consequences are mandatory for new work:

- applications do not bind directly to concrete hardware drivers when a semantic capability exists;
- drivers/providers do not independently initialize shared buses or globally owned hardware;
- streams have bounded buffering and explicit backpressure/overflow behavior;
- resources have an owning execution context and deterministic reclamation;
- persistent state does not retain raw pointers into unloadable ELFs;
- transport-specific details stay below semantic device/capability APIs;
- reusable network, storage, sensor, scheduling, notification, security, and data movement functions belong to platform services rather than individual applications;
- manifests describe requirements and handlers so routing/resolution can occur without loading candidate ELFs.

## Specification tree

### A. Platform contract and portability

Start with [Platform Abstraction Architecture](PLATFORM_ABSTRACTION_ARCHITECTURE.md). It defines the board-independent boundary, hardware/provider ownership, and capability-oriented platform contract.

Then use:

- [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md) for installable hardware/provider modules and capability resolution.
- [Runtime Driver Implementation](RUNTIME_DRIVER_IMPLEMENTATION.md) for the implemented driver subset and migration state.
- [Memory Architecture](MEMORY_ARCHITECTURE.md) for SRAM/PSRAM/storage-backed memory and mapping ownership.
- [Security Architecture](SECURITY_ARCHITECTURE.md) for trust, authorization, signed modules, permissions, and production policy.

### B. Application and execution model

Use [Scene Runtime Architecture](SCENE_RUNTIME_ARCHITECTURE.md) for application bundles, scenes/controllers, navigation state, lifecycle, and unloadable ELF execution.

Use [Service Runtime Architecture](SERVICE_RUNTIME_ARCHITECTURE.md) for background/system services, scheduling, events, persistence, and non-foreground work.

Application-facing guides are subordinate to these architecture specifications:

- [RiscRTE Applications](NATIVE_APPS.md) — current application ABI/framework and compatibility identifiers.
- [Adding Firmware Activities](ADDING_APPS.md) — legacy/in-firmware Activity path, only when functionality cannot yet be expressed through RiscRTE application/platform APIs.
- [RiscRTE UI Host API](NATIVE_UI_API.md) and [RiscRTE Network Host API](NATIVE_NETWORK_API.md) — current ABI/API documentation; historical symbol names may remain.

### C. Streams, IPC, and data movement

Use [Stream and Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md) as the canonical data-movement design. It implements Roadmap Part III and is the preferred mechanism for byte/record flows such as serial, GNSS, files, downloads, programming, recording, and transforms.

[Stream and Pipe MVP](STREAM_PIPE_MVP.md) documents the currently implemented/minimum transition slice. The MVP never overrides the architecture; it records what is safe to depend on today.

### D. Devices, sensors, and transports

The roadmap's **Unified Device and Peripheral Registry**, **Capability Resolver**, and **Unified Resource Ownership** are the parent abstractions for transport-specific specs.

- [Bluetooth Sensor Architecture](BLUETOOTH_SENSOR_ARCHITECTURE.md) is a transport-specific implementation/precursor. Its semantic sensor model generalizes into the roadmap's transport-independent Generic Sensor Framework.
- [GPS Driver](GPS_DRIVER.md) documents the current GNSS driver implementation. Forward consumers request `location.*` capabilities rather than depending on a GPS implementation.
- [USB OTG Host Architecture](USB_OTG_HOST_ARCHITECTURE.md) defines USB transport ownership, enumeration, hubs, class providers, and role handling beneath the unified device/capability model.
- [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md) defines programming/debugging as reusable capabilities/jobs over streams and device providers, not app-private probe stacks.

### E. Roadmap capability branches

The [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) is the authoritative expansion plan. New detailed specifications should branch from it in these domains:

1. Core platform unification — device registry, capability resolver, resource ownership.
2. Hardware and sensors — generic sensors, I2C/SPI/GPIO providers, location, observation/fusion.
3. Streams and IPC — streams/pipes, structured IPC, unified event bus.
4. Storage and data — volumes, recorder/time-series data, content intents, clipboard/share, search/indexing.
5. Networking and communications — reusable network services, message transport, discovery.
6. System facilities — notifications, alarms/deadlines, automation/rules.
7. Security services — credentials/secrets and policy integration.
8. Packaging/lifecycle — packages, dependency/capability declarations, install/update/remove lifecycle.
9. Power/reliability/diagnostics — ownership-aware power, watchdogs, health, logs, crash/diagnostic facilities.

A new architecture document SHOULD be linked from the relevant roadmap section and added to this tree when it becomes authoritative.

## Future-state versus current-state documentation

Specifications may need to describe both the target architecture and code that still implements an older design. Use this structure:

1. **Canonical / target architecture** — first, normative, aligned to this master spec and roadmap.
2. **Current implementation status** — what exists now, including gaps.
3. **Legacy compatibility** — old names, APIs, ownership patterns, or implementation details that must remain documented until migrated.
4. **Migration requirements** — how new work avoids deepening the legacy dependency and how existing code converges on the target.

Do not delete accurate legacy implementation documentation merely because the target changed. Move it beneath an explicit current/legacy heading and prepend the target design.

## Rules for new changes

Before implementing a platform change, an agent/contributor MUST:

1. read this file and the roadmap section governing the feature;
2. read the directly applicable child architecture spec(s);
3. inspect current implementation before assuming the spec is already implemented;
4. classify the change as application, service, provider/driver, stream/transform, device, job, intent/content handler, package, or core runtime primitive;
5. use semantic capabilities instead of concrete implementation dependencies where the roadmap defines them;
6. update the authoritative spec in the same change when architecture/API behavior changes;
7. preserve legacy documentation only where it still describes deployed/current code, and label it explicitly;
8. avoid introducing new `T5*` platform terminology except for hardware or compatibility identifiers.

## Documentation maintenance rule

Every architecture/API document should carry a short authority header pointing back to this master specification. Documents that describe a legacy or transitional subsystem must say so near the top. The roadmap and this index must be updated when a new authoritative branch spec is added.

This file is deliberately stable and concise enough to be the first document an automated coding agent reads. Detailed requirements belong in the linked child specifications, not in ad-hoc implementation notes.