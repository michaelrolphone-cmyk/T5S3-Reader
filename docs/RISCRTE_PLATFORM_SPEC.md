# RiscRTE Platform Specification

## Status and authority

This is the **canonical entry point for RiscRTE architecture and specification work**. All contributors and coding agents MUST read it before making architectural changes, adding APIs, apps, services, drivers/providers, or changing resource ownership. Follow this specification tree and [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md).

**Binding hardware invariant:** [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) governs all hardware, driver, transport, registry, capability and device work. The generic runtime regards `usb` as an opaque capability name, not a built-in subsystem. Historical hardware-ownership wording in other docs records legacy implementation only.

**Binding current-work plan:** [Next hardware-test milestone U1](NEXT_HARDWARE_TEST_MILESTONE.md) is the authoritative execution order, status correction and **single owner-test handoff** for completion of unified package management, removal of package-signing scope creep and full USB firmware extraction. For these tasks the revised [Package Manager Scope Contract](PACKAGE_MANAGER_SCOPE_CONTRACT.md), [Unified Package Manager MVP](UNIFIED_PACKAGE_MANAGER_MVP.md), [ordinary package format](RISC_PACKAGE_FORMAT.md) and [USB migration status](USB_ELF_MIGRATION_STATUS.md) override stale implementation/PR descriptions. **U1 additionally MUST deliver [Bundled Package Archive and Install Layout](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md): one ZIP-compatible `.rte.zip` distribution per package, including its manifest, executable and resources, installed into its own per-ID directory for all four kinds, with safe migration and a bootstrap ZIP reader independent of an installable ZIP service.** This owner-authorized addition supersedes earlier U1 text treating separate ELF/manifest assets as the target or fixed-four-file download paths as acceptable end state. U1's internal code checkpoints require no intermediate owner hardware test and no idle CI wait. Do not treat planning/spec changes as already implemented source changes. Do not merge, release, flash or claim USB hardware success merely because the U1 plan exists.

**Binding application version policy:** [Application Version Policy](APP_VERSION_POLICY.md) applies to **every** first-class app edit, including U1 changes to Serial Monitor, App Store, Driver Manager and Package Manager. Any distributable app source/behavior/resource/manifest change requires a strictly increased version in that app's own source `Apps/<app>.json` within the same PR. Firmware version or `min_firmware_version` changes do not count. The identical app version propagates to sidecar, package, catalog and installed inventory. Compare against prior target/published versions and do not reissue altered payloads under a previously released app identity/version. The current format validator is not a changed-source version-increase gate; manual checking is mandatory until automatic enforcement is implemented and tested. [Package Identity and Version Policy](PACKAGE_IDENTITY_VERSION_POLICY.md) enforces the same stable lineage/version rule across drivers, services and providers, including the CDC ID fork repair.

**Future deployment provisioning:** [Deployment Provisioning Manifest](DEPLOYMENT_PROVISIONING.md) specifies an eventual SD-card first-run desired-state manifest for board/hardware profiles, installed drivers/providers/apps and initial configuration, using the ordinary manager. It is **DEFERRED**, not part of U1 or the current package-manager MVP and not an instruction to implement a first-run installer or extra release gate. The new ZIP bundle format may serve future provisioning without implementing it now.

Precedence: explicit current user scope; this master specification and hardware invariant; roadmap where consistent; applicable normative child specs and current-work U1 contract; implementation/MVP reports; explicitly labeled historical/legacy notes. Legacy behavior is not design precedent. Signing, signer trust and cryptographic security-floor ideas in broad security/roadmap documents are **not** authorized package-manager MVP requirements; the current owner instruction is to purge package-signing implementation from the active repository while preserving integrity/loader safety. ZIP is a distribution container, not a new signed-package system.

## Naming

The platform/runtime is **RiscRTE (RISC Runtime Environment)**. `T5S3`, `T5 ePaper S3`, `T5S3 Pro` and `EPD47` are hardware names. Historic `T5*`, `native_*`, paths, ABI structures, build environments and artifact names remain only where needed to describe compatible/current implementation.

## Core platform invariant

New reusable functionality SHOULD normally be a **capability, provider, service, stream, device record, job, intent/content handler, package, or core primitive**, not private application infrastructure.

Applications declare what they require. The generic runtime resolves arbitrary capability names and supervises provider lifecycles; independently installed provider/driver ELFs implement and own hardware. The core MUST NOT implement, select, enumerate, reset or physically own USB or another device/protocol as a built-in subsystem. Drivers may consume other provider capabilities. An ELF proxy into a compiled-in hardware driver or a firmware-side VID/PID/class/provider bridge does not satisfy this invariant. Cross-module communication uses versioned handles, messages, events and streams, not persistent pointers across ELF lifetimes.

Every invocation has an owning **execution context**. Generic handles, software leases and subscriptions are bound to it and deterministically reclaimed; this bookkeeping is NOT firmware ownership of physical hardware. The providing ELF owns hardware operations, arbitration, state and safe teardown. Boundary-crossing resources SHOULD have generation-safe opaque handles, owner, type and rights.

A minimal, separately identified platform bootstrap/port MAY initialize CPU, memory, loader and boot-critical module storage and expose generic system primitives. It MUST NOT hide normal hardware implementations or silently replace absent/corrupt installed ELFs. Recovery hardware paths are isolated and cannot register as production providers. A different ESP CPU/ABI may require its own port; compatible new devices on an existing port require only ELF/profile installation and no RiscRTE core recompile.

### High-priority rule for new applications

**Trusted RiscRTE system UI mediation and private application storage are high priority for new app work, subject to explicit task scope.** Applications SHOULD request files/resources, devices, credentials, permissions and network selection through RiscRTE-owned pickers/intents, receiving scoped handles/results, rather than using broad private selectors. Store private state in package-private storage; shared content enters through trusted picker/intent/share/scoped handles. If a primitive is absent, implement the smallest authorized generic primitive rather than deepen the legacy broad-access model. See [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md). Distributed app resources and executable are installed together under one validated app package root; app-private mutable state belongs elsewhere.

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

The core MAY manage generic leases, not USB sessions or USB physical leases. Any driver MAY provide an opaque `usb` capability; only ELFs and board/profile data interpret USB discovery, topology, transfers, power and recovery. The transport-neutral registry receives provider-published records without enumerating a bus or interpreting descriptors.

Mandatory consequences:

- Applications request semantic capabilities instead of concrete hardware drivers wherever defined; installing hardware does not itself activate it or authorize access.
- Adding a new device/transport/chipset requires an ELF/profile, not a firmware source modification or hard-coded provider catalog/bridge.
- Major generic resources have execution-context ownership and deterministic reclamation; hardware state and arbitration belong to providers.
- No firmware hardware-specific managers, discovery loops, projections, protocol selection, physical stream, special-case loader route or silent fallback drivers.
- Cross-ELF handles are opaque/generation-safe where needed; retain mappings and dependency pins through uncertain hardware quiescence.
- Providers sharing a controller/bus coordinate through provider capabilities or generic opaque platform grants; core does not know bus semantics.
- Streams have bounded buffering and explicit backpressure; providers perform real I/O. Persistent state cannot retain unmapped ELF pointers.
- Hardware-dependent networking, storage, sensing, power and display belong to ELFs; generic event/job/notification/data-movement facilities may be core services.
- Manifests declare requirements without executing candidate ELFs. App resources SHOULD be accounted/quota-capable, requirements validated from SDK use where practical, and private storage/trusted mediation preferred.
- All four package kinds distribute as one bounded ZIP-compatible archive and install as one verified per-ID directory generation. An independently installable ZIP service cannot be a prerequisite for the ordinary package bootstrap.

### Unified device lifecycle observation

The generic runtime-owned device inventory SHALL accept bounded provider-published arrivals, state changes, capability loss and removal without running callbacks during mutation. Providers discover physical devices; the registry MUST NOT poll USB or other transport, parse hardware metadata, project firmware USB snapshots or start hardware for discovery. Provider callbacks/events SHALL marshal generic mutations to the owner task until explicitly synchronized.

Events have monotonic sequence, generation-qualified opaque handle, copied removal identity, prior/current state and revoked generic leases. Capability loss is reported even with zero leases. Repeated identical states do not emit duplicates. Identity semantics are provider-owned opaque metadata, not core VID/PID policy. Storage is bounded; per-consumer cursors get explicit overflow/GAP and must resnapshot before resuming. Observation does not grant device access. [Device Observation API](DEVICE_OBSERVATION_API.md) retains current-state limits; firmware `nativeDeviceDiscoveryTick` is legacy, not target.

### Application requirements and launch gating

[Application Capability Requirements](APP_CAPABILITY_REQUIREMENTS.md) validates bounded, versioned mandatory/optional declarations and resolves semantic providers **before ELF mapping**. Mandatory bindings belong to a unique execution context, release before unload and roll back atomically on failure. Declaring/binding a dependency does not grant hardware permission. Compatible undeclared legacy apps remain supported; full provider activation/permission enforcement is separate. Resolver uses manifests and names/versions, not USB/GPS/provider cases.

### Semantic access authorization

[Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md) adds versioned default-deny, generation- and execution-context-bound handles. Only trusted policy/UI grants scoped rights; an SD manifest cannot enlarge them. The generic core checks identity, rights and generation, revoking when a provider/invocation ends. Additive v3 `request` uses a one-run firmware permission prompt, rechecks context/identity after approval and requires fresh physical Confirm; touch remains deny-only until reliable touch confirmation. This remains transient consent, **not end-to-end hardware permission enforcement**: independently trustworthy UI, activation and provider-enforced access handles still require work in legacy integrations. Package signing is not a prerequisite for implementing these controls and is not authorized in current package work.

## Specification tree

### A. Platform contract, portability and current execution

- [Hardware-Agnostic Runtime and Driver Ownership Contract](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) — mandatory architecture for hardware work.
- [Next hardware-test milestone U1](NEXT_HARDWARE_TEST_MILESTONE.md) — **read for current USB, packages, ZIP bundling/layout, signing purge, catalog, Serial Monitor and provider release work; task order, source targets, acceptance and one hardware test handoff**.
- [Driver Platform Reuse Acceptance](DRIVER_PLATFORM_REUSE_ACCEPTANCE.md) — mandatory cross-family U1 gate and non-USB witness.
- [Package Manager Scope Contract](PACKAGE_MANAGER_SCOPE_CONTRACT.md) — explicit scope and crypto purge requirement.
- [Bundled Package Archive and Install Layout](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md) — **mandatory U1 one-ZIP-per-package distribution, owned per-ID installation, ZIP service/bootstrap and migration acceptance**.
- [Stable Package Identity and Version Policy](PACKAGE_IDENTITY_VERSION_POLICY.md) — version/update identity rules, CDC fork repair and guards.
- [Platform Abstraction Architecture](PLATFORM_ABSTRACTION_ARCHITECTURE.md) — execution model; conflicting hardware ownership is legacy.
- [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md) — normative driver/capability model, constrained by hardware boundary and U1 package scope.
- [Runtime Driver Implementation](RUNTIME_DRIVER_IMPLEMENTATION.md) — migration status and legacy facts, not normative hardware ownership.
- [USB ELF Migration Status](USB_ELF_MIGRATION_STATUS.md) — current real ELF foundation and remaining firmware residue.
- [Memory Architecture](MEMORY_ARCHITECTURE.md)
- [RiscRTE System Storage Layout](SYSTEM_STORAGE_LAYOUT.md) — normative ownership and semantics for `/System/Registry`, `State`, `Config`, `Cache`, `Logs` and `Recovery`; new code must not create `/.crosspoint` state.
- [Security Architecture](SECURITY_ARCHITECTURE.md) — longer-term scope, not a package-signing instruction.

### B. Application and execution model

- [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md) — contexts, owned resources, trusted UI and private storage; required for new apps.
- [Application Capability Requirements](APP_CAPABILITY_REQUIREMENTS.md) — manifest resolution and invocation binding.
- [Scene Runtime Architecture](SCENE_RUNTIME_ARCHITECTURE.md)
- [Service Runtime Architecture](SERVICE_RUNTIME_ARCHITECTURE.md)
- [RiscRTE Applications](NATIVE_APPS.md) — current ABI and compatibility names.
- [Application Version Policy](APP_VERSION_POLICY.md) — mandatory per-app version bump for any distributable app change.
- [Adding Firmware Activities](ADDING_APPS.md) — legacy in-firmware activity path.
- [RiscRTE UI Host API](NATIVE_UI_API.md)
- [RiscRTE Network Host API](NATIVE_NETWORK_API.md) — compatibility API, not a firmware hardware-driver precedent.

### C. Streams, IPC and data movement

- [Stream and Pipe Architecture](STREAM_PIPE_ARCHITECTURE.md) — canonical generic design.
- [Typed Record Stream ABI](STREAM_RECORD_API_V2.md) — ELF record interface and migration status.
- [GNSS Record/Provider Publication](STREAM_GNSS_RECORD_ADAPTER.md) — GNSS schema and current limits.
- [GNSS Registry Integration](STREAM_GNSS_REGISTRY_INTEGRATION.md) — registry/lease integration.
- [Stream and Pipe MVP](STREAM_PIPE_MVP.md) — existing transition subset.

Generic streams are preferred for serial, files, downloads, GNSS, programming, recording and transforms; physical I/O remains in providers.

### D. Devices, sensors and transports

The generic registry/resolver/context facilities are parent abstractions for provider-published capabilities, not transport owners. All device-specific specs place normal hardware inside ELFs.

- [Device Observation API](DEVICE_OBSERVATION_API.md) — events/inventory and gaps.
- [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md) — rights, versions, transient consent and provider gaps.
- [Bluetooth Sensor Architecture](BLUETOOTH_SENSOR_ARCHITECTURE.md)
- [GPS Driver](GPS_DRIVER.md)
- [USB OTG Host Architecture](USB_OTG_HOST_ARCHITECTURE.md) — older firmware ownership claims overridden by hardware contract and U1.
- [USB Host Startup and Detection](USB_HOST_STARTUP_AND_DETECTION.md) — mandatory USB implementation guide: role before VBUS, detection stages, safe cleanup and input semantics; records the owner-confirmed controller 0.1.14 fix.
- [Firmware Input Navigation](FIRMWARE_INPUT_NAVIGATION.md) — installed navigation provider, keyboard/gamepad controls and foreground application handoff.
- [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md) — legacy snapshot, not target.
- [Programmer/Debugger Architecture](PROGRAMMER_DEBUGGER_ARCHITECTURE.md)

### E. Roadmap and package branches

[Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md) is an expansion plan, **not authorization to implement adjacent features**:

1. Generic registry, resolver, execution contexts and ownership.
2. Provider-owned hardware/sensors/fusion, no built-in managers.
3. Streams, IPC and event bus.
4. Trusted resource mediation and private storage before broad storage/data facilities.
5. Networking/communications.
6. System facilities.
7. Security services (future separately scoped work).
8. Package management — [ordinary format](RISC_PACKAGE_FORMAT.md), [bundled archive/install layout](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md), [Unified Package Manager MVP](UNIFIED_PACKAGE_MANAGER_MVP.md), [Package Manager Scope Contract](PACKAGE_MANAGER_SCOPE_CONTRACT.md), and [U1 integrated completion](NEXT_HARDWARE_TEST_MILESTONE.md). Existing loose packages are a real foundation; current U1 must complete four-kind ZIP distribution, per-ID directories, online/inventory/UI, signing purge and USB catalog removal. **No signing gate.**
9. Power, reliability, diagnostics and context accounting.

**Deferred deployment extension:** [Deployment Provisioning Manifest](DEPLOYMENT_PROVISIONING.md) — future first-run SD desired-state installation of compatible board/hardware profiles, drivers/providers/apps and initial configuration through the ordinary manager. This is not a new U1 or MVP dependency.

## Future-state versus current-state documentation

Document in order: (1) target architecture, (2) implementation status, (3) legacy compatibility, (4) migration requirements. Do not erase truthful historical behavior; label still-compiled hardware paths **CURRENT/LEGACY, NONCOMPLIANT**. For package-signing sources specifically, the user has required a physical purge from the active tree; explaining their prior existence does not authorize preserving the implementation or its active experimental spec. Historical loose distribution remains an explicit migration input, not the new desired end state.

## Rules for new changes

Before a change, read this master and governing roadmap/child spec; read hardware boundary for device/capability/driver work and [U1](NEXT_HARDWARE_TEST_MILESTONE.md) plus [bundled package spec](BUNDLED_PACKAGE_ARCHIVE_AND_INSTALL_LAYOUT.md) for package/USB/Serial Monitor/archive/layout work. Any new RiscRTE-owned SD state or generated system metadata must follow [RiscRTE System Storage Layout](SYSTEM_STORAGE_LAYOUT.md). For app work, read execution-context architecture and apply trusted UI/private storage within scope. Inspect code before treating a target as implemented. Use semantic capabilities and appropriate platform abstractions, never create new hardware-specific core bridges. Associate software grants with execution-context owners. Update normative specs with architectural/API changes and label remaining legacy behavior. Use RiscRTE naming for the platform.

One explicit user request controls change scope; U1 is a single software milestone with internal workstreams and one owner hardware-test handoff. Do not require physical testing between commits, wait idle on CI instead of progressing independent work, conflate CI and hardware acceptance, create stacked interdependent PRs, or auto-merge/release/flash. Preserve a recoverable progress ledger across prompts.

## Documentation maintenance rule

Every architecture/API document should link to this master. Transitional subsystems should state their status near the top. Update this index and roadmap on addition of an authoritative branch spec. This master remains an entry point; detailed requirements and actionable work reside in linked children.
