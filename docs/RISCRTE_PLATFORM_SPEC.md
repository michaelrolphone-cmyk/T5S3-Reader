# RiscRTE Platform Specification

## Status and authority

This is the **canonical entry point for RiscRTE architecture and specification work**.

All contributors and coding agents MUST read this document before making architectural changes, adding platform APIs, adding applications/services/drivers/providers, or changing resource ownership. New work MUST follow this specification tree and the [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md).

Precedence: this master specification and invariants; the roadmap; applicable architecture specifications; implementation/MVP documents; then explicitly labeled legacy implementation notes. Legacy implementation is not precedent for new design.

## Naming

The platform/runtime is **RiscRTE (RISC Runtime Environment)**. `T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware names. Historical `T5*`, `native_*`, paths, ABI structures, build environments and artifacts remain only where needed to describe compatible/current implementation.

## Core platform invariant

New platform functionality SHOULD normally be a reusable **capability, provider, service, stream, device, job, intent/content handler, package, or core runtime primitive**, not private application infrastructure.

Applications state what they require. The runtime resolves implementations. Hardware access belongs below bounded runtime-owned abstractions. Cross-module communication uses versioned handles/messages/events/streams rather than persistent raw pointers across ELF lifetimes.

Every application invocation has an owning **execution context**. Resources are owned by that context and deterministically reclaimed. Resources crossing the application/runtime boundary SHOULD converge on generation-safe opaque handles with ownership, type and rights metadata.

### High-priority rule for new applications

**Trusted RiscRTE system UI mediation and private application storage are high-priority platform requirements for new app work.**

New applications SHOULD request files/resources, devices, credentials, permissions, networks and similar security-sensitive choices through RiscRTE-owned pickers/intents and receive scoped handles/results rather than implementing broad app-private selection infrastructure.

New applications SHOULD store private state in a package-private logical storage namespace rather than inventing shared `/sd` paths. User/shared content SHOULD normally enter through a trusted picker, intent/share operation or scoped resource handle.

If a required trusted picker or private-storage primitive does not yet exist, new app work SHOULD implement the smallest reusable platform primitive rather than deepen the legacy broad-access model. See [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

## Canonical architecture

```text
                         RiscRTE Applications
                                  |
                     intents / jobs / capabilities
                                  |
                       Execution Context
                                  |
                         Opaque Handles
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
                    Resource / Lease Owner
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

Mandatory consequences for new work:

- applications do not bind directly to concrete hardware drivers when a semantic capability exists;
- every major resource has an owning execution context and deterministic reclamation;
- handles crossing unloadable-module boundaries are opaque and generation-safe where reuse is possible;
- drivers/providers do not independently initialize shared buses or globally owned hardware;
- streams have bounded buffering and explicit backpressure/overflow behavior;
- persistent state does not retain raw pointers into unloadable ELFs;
- transport details stay below semantic device/capability APIs;
- reusable network, storage, sensor, scheduling, notification, security and data-movement functions belong to platform services;
- manifests describe requirements and handlers without loading candidate ELFs;
- application memory/resources SHOULD be accounted and quota-capable;
- build tooling SHOULD derive or validate manifest requirements from SDK/API use where practical;
- security-sensitive resource selection SHOULD use trusted system UI and scoped authority;
- application-private state SHOULD use private package storage rather than unrestricted shared-volume access.

### Unified device lifecycle observation

The runtime-owned device inventory SHALL expose bounded observation of device arrival, meaningful state changes, capability loss, and removal without invoking application or driver callbacks during registry mutation. Transport callbacks SHALL publish snapshots and marshal inventory mutations to the owning runtime task until the registry has an explicitly synchronized implementation.

Each event SHALL have a monotonic sequence, an opaque generation-qualified device handle, copied identity sufficient to correlate a removal after its handle is invalid, prior and current states, and the number of leases forcibly revoked by that transition. A transition from AVAILABLE/BUSY to an unusable state, or removal of a usable device, SHALL report capability loss even when no consumer currently holds a lease. Repeated identical state observations SHALL not generate duplicate events.

Event storage SHALL be bounded. Consumers SHALL use independently maintained cursors, be told explicitly when retained events were overwritten, and re-enumerate the current registry before resuming after a gap. A consumer MUST NOT interpret the retained suffix as a complete event history after an overflow. Observation alone SHALL NOT grant access to a device; acquiring and using capability leases remains subject to execution-context identity and rights. This currently describes a firmware-internal owner-task journal, not an authorization-free ELF ABI or a complete cross-task event bus.

## Specification tree

### A. Platform contract and portability

- [Platform Abstraction Architecture](PLATFORM_ABSTRACTION_ARCHITECTURE.md)
- [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md)
- [Runtime Driver Implementation](RUNTIME_DRIVER_IMPLEMENTATION.md) — current/migration state
- [Memory Architecture](MEMORY_ARCHITECTURE.md)
- [Security Architecture](SECURITY_ARCHITECTURE.md)

### B. Application and execution model

- [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md) — authoritative for execution contexts, universal object ownership, trusted system UI, private app storage, resource quotas and manifest derivation. **Required reading for new app work.**
- [Scene Runtime Architecture](SCENE_RUNTIME_ARCHITECTURE.md)
- [Service Runtime Architecture](SERVICE_RUNTIME_ARCHITECTURE.md)
- [RiscRTE Applications](NATIVE_APPS.md) — current application ABI/framework and compatibility identifiers
- [Adding Firmware Activities](ADDING_APPS.md) — legacy/in-firmware Activity path
- [RiscRTE UI Host API](NATIVE_UI_API.md)
- [RiscRTE Network Host API](NATIVE_NETWORK_API.md)

### C. Streams, IPC, and data movement

- [Stream and Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md) — canonical data-movement design
- [Stream and Pipe MVP](STREAM_PIPE_MVP.md) — implemented/transition subset

Streams/pipes are the preferred reusable path for serial, files, downloads, GNSS, programming, recording and transforms.

### D. Devices, sensors, and transports

The Unified Device/Peripheral Registry, Capability Resolver and Unified Resource Ownership are parent abstractions for transport-specific specs.

- [Bluetooth Sensor Architecture](BLUETOOTH_SENSOR_ARCHITECTURE.md)
- [GPS Driver](GPS_DRIVER.md)
- [USB OTG Host Architecture](USB_OTG_HOST_ARCHITECTURE.md)
- [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md)

### E. Roadmap capability branches

The [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) is the authoritative expansion plan:

1. Core platform unification — device registry, resolver, resource ownership, execution contexts/object handles.
2. Hardware/sensors — generic sensors, I2C/SPI/GPIO, location, observation/fusion.
3. Streams/IPC — streams/pipes, structured IPC, event bus.
4. Storage/data — **private app storage and trusted file/resource mediation first**, then volumes, recorder, content intents, clipboard/share, search/indexing.
5. Networking/communications.
6. System facilities.
7. Security services.
8. Packaging/lifecycle — manifests, requirements, dependency declarations, install/update/remove lifecycle.
9. Power/reliability/diagnostics — including per-context resource accounting.

## Future-state versus current-state documentation

When target architecture and legacy/current implementation differ, document in this order:

1. **Canonical / target architecture**.
2. **Current implementation status**.
3. **Legacy compatibility**.
4. **Migration requirements**.

Do not delete accurate legacy implementation documentation merely because the target changed. Move it beneath an explicit current/legacy heading and prepend the target design.

## Rules for new changes

Before implementing a platform change, an agent/contributor MUST:

1. read this file and the governing roadmap section;
2. read directly applicable child architecture specs;
3. for application work, read `APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md` and prioritize trusted system UI/private storage over app-private broad access;
4. inspect current implementation before assuming the target is implemented;
5. classify the change using platform abstractions rather than creating private infrastructure;
6. use semantic capabilities instead of concrete implementation dependencies where defined;
7. route newly acquired resources to an execution-context owner and prefer opaque handles;
8. update the authoritative spec in the same change when architecture/API behavior changes;
9. preserve/labeled legacy documentation where it still describes deployed code;
10. avoid new `T5*` platform terminology except hardware/compatibility identifiers.

## Documentation maintenance rule

Every architecture/API document should point back to this master specification. Transitional/legacy subsystems must say so near the top. The roadmap and this index must be updated when a new authoritative branch spec is added.

This file is deliberately stable and concise enough to be the first document an automated coding agent reads. Detailed requirements belong in linked child specifications.
