# RiscRTE Platform Specification

## Status and authority

This is the **canonical entry point for RiscRTE architecture and specification work**.

All contributors and coding agents MUST read this document before making architectural changes, adding platform APIs, adding applications/services/drivers/providers, or changing resource ownership. New work MUST follow this specification tree and the [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md).

**Binding hardware invariant:** [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) is mandatory for all hardware, driver, transport, registry, capability, and device work. It refines this master specification and supersedes conflicting hardware-ownership instructions in historical child documents and roadmap implementation examples. The capability manager treats `usb` exactly as it treats every other arbitrary capability identifier: as data, not as a built-in subsystem.

Precedence: this master specification and hardware-agnostic invariants; the roadmap where consistent; applicable architecture specifications; implementation/MVP documents; then explicitly labeled legacy implementation notes. Legacy implementation is not precedent for new design.

## Naming

The platform/runtime is **RiscRTE (RISC Runtime Environment)**. `T5S3`, `T5 ePaper S3`, `T5S3 Pro`, and `EPD47` are hardware names. Historical `T5*`, `native_*`, paths, ABI structures, build environments and artifacts remain only where needed to describe compatible/current implementation.

## Core platform invariant

New platform functionality SHOULD normally be a reusable **capability, provider, service, stream, device record, job, intent/content handler, package, or core runtime primitive**, not private application infrastructure.

Applications state what they require. The generic runtime resolves opaque capability identifiers and supervises provider lifecycles; installable provider/driver ELFs implement and own the underlying hardware. The core MUST NOT implement, select, enumerate, reset, or own USB or any other hardware protocol/device as a built-in subsystem. Drivers may depend on capabilities supplied by other ELFs. A descriptor parser or forwarding proxy calling a compiled-in hardware driver is not an installable hardware driver. Cross-module communication uses versioned handles/messages/events/streams rather than persistent raw pointers across ELF lifetimes.

Every application invocation has an owning **execution context**. Generic handles, leases, and subscriptions are associated with that context and deterministically reclaimed. This is ownership of authorization and software resource lifetimes, **not ownership of physical hardware by the framework**. The providing driver owns hardware operations, states, concurrency, arbitration (possibly via another provider), and safe teardown. Resources crossing the application/runtime boundary SHOULD converge on generation-safe opaque handles with ownership, type and rights metadata.

A minimal, separately identified platform bootstrap/port layer MAY initialize the CPU, memory, loader and boot-critical module storage and expose generic system primitives. It MUST NOT be used to hide normal device implementations or silently replace absent/corrupt driver ELFs. Recovery-only hardware access is isolated from ordinary capability provision and documented as such.

### High-priority rule for new applications

**Trusted RiscRTE system UI mediation and private application storage are high-priority platform requirements for new app work.**

New applications SHOULD request files/resources, devices, credentials, permissions, networks and similar security-sensitive choices through RiscRTE-owned pickers/intents and receive scoped handles/results rather than implementing broad app-private selection infrastructure.

New applications SHOULD store private state in a package-private logical storage namespace rather than inventing shared `/sd` paths. User/shared content SHOULD normally enter through a trusted picker, intent/share operation or scoped resource handle.

If a required trusted picker or private-storage primitive does not yet exist, new app work SHOULD implement the smallest reusable platform primitive rather than deepen the legacy broad-access model. See [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

## Canonical architecture

```text
                Application / Service ELFs
                          |
             abstract capability requests
                          |
          Generic RiscRTE core (hardware-blind)
          execution contexts / policy / handles
          manifest graph / resolver / streams
          generic lifecycle / events / loader
                          |
               installed provider ELFs
         +----------------+----------------+
         |                |                |
   device drivers   composite providers   services
         |                |                |
         +---- provider-to-provider -------+
                        dependencies
                          |
           hardware implementation in ELFs
                          |
                platform port primitives
                          |
                       hardware
```

The core MAY manage generic leases, not USB sessions or USB-specific physical leases. A driver MAY supply a `usb` capability; no core compile-time knowledge of USB is implied. The driver or a dependency provider performs discovery, controller management, USB topology, transfers, power control, and recovery. A transport-neutral device registry receives provider-supplied records; it does not enumerate a bus or interpret device descriptors.

Mandatory consequences for new work:

- applications do not bind directly to concrete hardware drivers when a semantic capability exists;
- adding a new device/transport or chipset requires an ELF and data, not a firmware source modification;
- every major generic resource has an owning execution context and deterministic reclamation;
- hardware-specific runtime managers, discovery loops, device projections, protocol branches and fallback drivers are prohibited in the target core;
- handles crossing unloadable-module boundaries are opaque and generation-safe where reuse is possible;
- drivers/providers that share hardware coordinate through a provider capability or a generic opaque platform resource grant; the core does not know bus semantics;
- streams have bounded buffering and explicit backpressure/overflow behavior while actual device I/O belongs to the provider;
- persistent state does not retain raw pointers into unloadable ELFs;
- transport details stay in provider ELFs, below semantic application capabilities, and outside the core;
- reusable hardware-dependent network, storage, sensor and power functions belong to loadable providers; generic scheduling, notification, security and data-movement mechanisms may be core services;
- manifests describe requirements and handlers without loading candidate ELFs;
- application memory/resources SHOULD be accounted and quota-capable;
- build tooling SHOULD derive or validate manifest requirements from SDK/API use where practical;
- security-sensitive resource selection SHOULD use trusted system UI and scoped authority;
- application-private state SHOULD use private package storage rather than unrestricted shared-volume access.

### Unified device lifecycle observation

The generic runtime-owned device inventory SHALL accept bounded provider-published observations of device arrival, meaningful state changes, capability loss and removal without invoking application/driver callbacks during registry mutation. A driver is responsible for physical discovery and publishing its records. The registry MUST NOT poll USB or any transport, parse hardware-specific metadata, project firmware USB snapshots, or start hardware for discovery. Provider callbacks/events SHALL marshal generic registry mutations to the registry's owning task until it has an explicitly synchronized implementation.

Each event SHALL have a monotonic sequence, an opaque generation-qualified device handle, copied identity sufficient to correlate removal after invalidation, prior and current states, and number of generic capability leases revoked. Losing availability SHALL report capability loss even if no consumer has a lease. Repeated identical state observations SHALL not generate duplicate events. Identity semantics are provider-defined metadata, not core USB VID/PID logic.

Event storage SHALL be bounded. Consumers SHALL use independently maintained cursors, be told explicitly when retained events were overwritten, and re-enumerate the current registry before resuming after a gap. A consumer MUST NOT interpret the retained suffix as a complete event history after an overflow. Observation alone SHALL NOT grant hardware access; acquiring and using capability leases remains subject to execution-context identity and rights. The initial [Device Observation API](DEVICE_OBSERVATION_API.md) documents existing current-state limits; a legacy firmware-side USB discovery tick is NOT the target publication mechanism.

### Application capability requirements and launch gating

The [Application Capability Requirements](APP_CAPABILITY_REQUIREMENTS.md) implementation validates bounded, versioned mandatory/optional manifest declarations and resolves available, versioned semantic providers **before mapping an ELF**. Mandatory dependencies are transactionally bound to a unique execution context before the load and released before unload, with all-or-nothing rollback. A declaration or dependency binding grants no hardware access. Legacy undeclared apps remain compatible. The complete architecture still requires provider activation, trusted permissions and enforcement of public semantic access handles by each provider. The resolver MUST use names/versions and manifests, not hard-coded USB/GPS/provider cases.

### Semantic access authorization — implementation boundary

The [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md) extends observation with versioned, default-deny access handles. Only trusted policy/UI may grant a scoped right for one device generation and active execution context; an SD manifest cannot issue or expand grants. The generic core checks owner, rights and generation, revoking handles when a grant/provider disappears or the invocation ends. The additive v3 `request` entry implements a firmware-owned one-run permission prompt and rechecks invocation and identity after approval. Approval requires a fresh physical Confirm press; touch is deny-only until reliable new-touch detection exists. This is **transient consent, not end-to-end hardware permission enforcement**: signed package identity, independently authenticated UI, provider activation and access-handle enforcement inside older USB/GNSS APIs remain outstanding. No new work may mistake an inventory record or dependency lease for authorization or treat authorization as core ownership of hardware.

## Specification tree

### A. Platform contract and portability

- [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) — **mandatory normative hardware boundary, overrides historical conflicting driver/transport descriptions**
- [Platform Abstraction Architecture](PLATFORM_ABSTRACTION_ARCHITECTURE.md) — retain documented execution model; hardware-ownership passages are legacy where conflicting
- [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md) — driver/capability architecture governed by hardware boundary
- [Runtime Driver Implementation](RUNTIME_DRIVER_IMPLEMENTATION.md) — current/migration state, not normative ownership
- [Memory Architecture](MEMORY_ARCHITECTURE.md)
- [Security Architecture](SECURITY_ARCHITECTURE.md)

### B. Application and execution model

- [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md) — authoritative for execution contexts, universal object ownership, trusted system UI, private app storage, resource quotas and manifest derivation. **Required reading for new app work.**
- [Application Capability Requirements](APP_CAPABILITY_REQUIREMENTS.md) — manifest requirements, generic versioned resolution and invocation-owned launch bindings, with explicit remaining limitations
- [Scene Runtime Architecture](SCENE_RUNTIME_ARCHITECTURE.md)
- [Service Runtime Architecture](SERVICE_RUNTIME_ARCHITECTURE.md)
- [RiscRTE Applications](NATIVE_APPS.md) — current application ABI/framework and compatibility identifiers
- [Adding Firmware Activities](ADDING_APPS.md) — legacy/in-firmware Activity path
- [RiscRTE UI Host API](NATIVE_UI_API.md)
- [RiscRTE Network Host API](NATIVE_NETWORK_API.md) — current compatibility API, not precedent for embedded network hardware drivers

### C. Streams, IPC, and data movement

- [Stream and Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md) — canonical generic data-movement design
- [Typed Record Stream ABI](STREAM_RECORD_API_V2.md) — versioned ELF record API, compatibility, ownership and implementation status
- [GNSS Record/Provider Publication](STREAM_GNSS_RECORD_ADAPTER.md) — schema and existing migration limits; no firmware GPS implementation permitted in target
- [GNSS Registry Integration](STREAM_GNSS_REGISTRY_INTEGRATION.md) — existing shared registry/lease integration boundary
- [Stream and Pipe MVP](STREAM_PIPE_MVP.md) — implemented/transition subset

Streams/pipes are the preferred reusable path for serial, files, downloads, GNSS, programming, recording and transforms. Generic streams do not perform hardware protocol I/O themselves.

### D. Devices, sensors, and transports

The generic Device Registry, Capability Resolver and execution-context bookkeeping are parent abstractions for driver-published capabilities, not owners of transports. Device-specific specifications must place all normal hardware implementation in ELFs.

- [Device Observation API](DEVICE_OBSERVATION_API.md) — authenticated inventory/event ABI and acceptance gaps
- [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md) — default-deny semantic rights, v2/v3 ABI, transient consent and incomplete provider enforcement
- [Bluetooth Sensor Architecture](BLUETOOTH_SENSOR_ARCHITECTURE.md)
- [GPS Driver](GPS_DRIVER.md)
- [USB OTG Host Architecture](USB_OTG_HOST_ARCHITECTURE.md) — legacy framework USB-ownership claims superseded by hardware contract
- [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md) — migration/legacy snapshot; ELF ownership is target
- [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md)

### E. Roadmap capability branches

The [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) is the authoritative expansion plan subject to the hardware boundary:

1. Core platform unification — generic registry, resolver, resource ownership, execution contexts/object handles.
2. Hardware/sensors — provider-owned hardware, sensors and fusion; no built-in hardware managers.
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

Do not delete accurate legacy implementation documentation merely because the target changed. Move it beneath an explicit current/legacy heading and prepend the target design. For hardware-specific code left in compiled firmware, explicitly label it **CURRENT/LEGACY, NONCOMPLIANT** rather than describing it as the target provider architecture.

## Rules for new changes

Before implementing a platform change, an agent/contributor MUST:

1. read this file and the governing roadmap section;
2. read [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) for any hardware/capability/registry/driver change and applicable child specs;
3. for application work, read `APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md` and prioritize trusted system UI/private storage over app-private broad access;
4. inspect current implementation before assuming the target is implemented;
5. classify the change using platform abstractions rather than creating private infrastructure;
6. use semantic capabilities instead of concrete implementation dependencies where defined;
7. place hardware protocol/control/discovery and power logic in provider ELFs, never new firmware-side hardware bridges;
8. route generic grants and other software resources to execution-context owners and prefer opaque handles;
9. update the authoritative spec in the same change when architecture/API behavior changes;
10. preserve/labeled legacy documentation where it still describes deployed code;
11. avoid new `T5*` platform terminology except hardware/compatibility identifiers.

## Documentation maintenance rule

Every architecture/API document should point back to this master specification. Transitional/legacy subsystems must say so near the top. The roadmap and this index must be updated when a new authoritative branch spec is added.

This file is deliberately stable and concise enough to be the first document an automated coding agent reads. Detailed requirements belong in linked child specifications.