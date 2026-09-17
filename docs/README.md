# RiscRTE Documentation

For architecture or implementation work, begin with **[RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md)**. It is the master specification and defines precedence, naming, invariants and the branch map. Then read the **[Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md)** section governing the change and the applicable child specification.

**Scope is bounded by the user's explicit current request.** Roadmap items are future directions, not authorization to implement related features or turn them into acceptance gates. Every implementation PR must declare its authorized deliverables, excluded/deferred work, tests and justification for mandatory dependencies. Consult repository-root [AGENTS.md](../AGENTS.md) for the mandatory change-control and scope-audit procedure.

**For package-manager or driver-loader work, read [Package Manager Scope Contract](PACKAGE_MANAGER_SCOPE_CONTRACT.md) before older package specifications.** It is the normative current-MVP amendment to roadmap package §§26–28 and to PR #76/#78 historical signed-package acceptance statements: unified management does not require signatures, signer trust, signed provenance or cryptographic rollback floors. SHA-256 integrity checks and independent runtime permissions remain in scope. Existing signed code has not thereby been removed.

**For every new application or application API change, also read [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md). Trusted RiscRTE resource pickers/system UI and package-private application storage are long-term/high-priority directions; implementation must remain within the current authorized scope.**

```text
RISCRTE_PLATFORM_SPEC.md                         <- START HERE
 |
 +-- PACKAGE_MANAGER_SCOPE_CONTRACT.md           <- current package MVP and scope-change authority
 |
 +-- PLATFORM_CAPABILITY_ROADMAP.md              <- platform direction, NOT blanket task scope
 |
 +-- Platform contract / runtime
 |    +-- PLATFORM_ABSTRACTION_ARCHITECTURE.md
 |    +-- RUNTIME_DRIVER_ARCHITECTURE.md
 |    +-- RUNTIME_DRIVER_IMPLEMENTATION.md       [migration/current state]
 |    +-- MEMORY_ARCHITECTURE.md
 |    +-- SECURITY_ARCHITECTURE.md
 |
 +-- Applications / execution
 |    +-- APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md [new-app architecture; within approved scope]
 |    +-- SCENE_RUNTIME_ARCHITECTURE.md
 |    +-- SERVICE_RUNTIME_ARCHITECTURE.md
 |    +-- NATIVE_APPS.md                          [RiscRTE apps; legacy filename/ABI]
 |    +-- NATIVE_UI_API.md                        [RiscRTE UI host; compatibility ABI]
 |    +-- NATIVE_NETWORK_API.md                   [RiscRTE network host; compatibility ABI]
 |    +-- ADDING_APPS.md                          [legacy compiled Activity path]
 |
 +-- Streams / IPC / data movement
 |    +-- STREAM_PIPE_ARCHITECTURE.md             [authoritative]
 |    +-- STREAM_PIPE_MVP.md                      [implemented subset]
 |
 +-- Devices / sensors / transports
      +-- BLUETOOTH_SENSOR_ARCHITECTURE.md        [BLE precursor to generic sensors]
      +-- GPS_DRIVER.md                           [GNSS/location migration state]
      +-- USB_OTG_HOST_ARCHITECTURE.md
      +-- PROGRAMMER_DEBUGGER_ARCHITECTURE.md
```

## Application architecture priorities

The following are target architecture rules, not standing permission to add unrelated features to a user task. New application work follows applicable rules while compatibility APIs still expose broader access:

- every invocation converges on a first-class execution context owning its resources;
- runtime-facing resources converge on generation-safe opaque handles/object ownership;
- **file/resource/device/network/credential/permission selection should use trusted RiscRTE-owned UI and return scoped authority;**
- **application-private state should use package-private storage, not new arbitrary shared `/sd` paths;**
- memory and resource use should be attributable/quota-capable per execution context;
- build tooling should derive or validate manifest requirements from SDK/API use where practical.

If a reusable platform primitive is missing, identify the dependency and either implement the smallest directly necessary piece within approved scope or track it as separate work. Do not silently expand the feature or make an unrelated roadmap facility mandatory.

## Roadmap branches awaiting/demanding dedicated specs

When implementation of an explicitly authorized feature begins to make one of these contracts concrete, create or promote a child architecture spec and link it from both the roadmap and master spec: Unified Device/Peripheral Registry; expanded Capability Resolver and resource leases; generic Sensor Framework; I2C/SPI/GPIO providers; Location/Observation/Fusion; structured IPC/Event Bus; unified Storage/Volumes and Recorder; content intents/share/search; reusable Networking/Communications/Discovery; Notifications/Alarms/Automation; credential vault; package/dependency lifecycle; power/reliability/diagnostics.

Do not create an app-private substitute for an applicable existing platform facility solely because a child spec is missing. Conversely, do not implement an unrelated facility just because it is present in the roadmap.

## Naming and legacy documentation

The platform is **RiscRTE**. T5S3/EPD47 are board identifiers. Historical `T5*`, `t5_*`, `native_*`, concrete class names, package names and source paths remain only where needed to identify deployed/current code or preserve ABI/source references. Legacy filenames may remain to avoid breaking links; document titles and forward-looking concepts use RiscRTE terminology.

When a document must describe an older implementation, structure it as target contract first, current implementation second, legacy compatibility where needed, then migration requirements. Accurate legacy state should not be deleted, but it does not override the target architecture or current explicit user scope.

## Supporting/operational documentation

`RELEASING.md`, board adaptation notes, activity-manager implementation notes, file/font/i18n/format guides, web-server docs and troubleshooting are supporting documentation. They may describe current code but do not override the architecture hierarchy above.

Repository-root `AGENTS.md` directs automated agents to this hierarchy. Architecture/API changes must update the applicable authoritative specification and include a scope audit in the same change.