# RiscRTE Application Capability Requirements — Launch Gating and Dependency Bindings

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This describes the current implementation, not a substitute for the target authorization and provider-activation model.

## Manifest contract

Native application JSON sidecars MAY declare up to six mandatory `requires` and six `optional` capabilities. New apps SHOULD declare actual dependencies; legacy sidecars without these keys remain compatible until migrated:

```json
{
  "requires": [{"capability": "location.position", "api": ">=1"}],
  "optional": [{"capability": "serial.port", "api": ">=1"}]
}
```

These keys supplement display name, ELF filename, minimum firmware version, icon and version fields. Capability names start with lowercase ASCII and continue with lowercase letters, digits, period, underscore or hyphen; maximum 39 visible bytes. Only `>=N` is supported, N in 1–65535 without leading zero. Lists are arrays of objects with exactly `capability` and `api`. Duplicates across both lists, malformed constraints or fields, invalid types, and excess entries fail validation. Sidecars retain the 2048-byte limit. The Python build validator and firmware parser both enforce declarations even if the caller does not request requirements. `optional` is validated but never blocks launch. Neither `t5_app_manifest_t` nor the ELF app ABI layout changes.

## Provider metadata and selection

The firmware-owned `DeviceRegistry` copies a version per declared capability from the trusted `Descriptor.capabilityApiVersions` array into each `DeviceInfo`. A missing version is represented as zero and **cannot** satisfy a version-constrained mandatory requirement. The generic resolver no longer infers versions from hard-coded ELF or provider names. The onboard GNSS adapter currently publishes `location.position` at the existing GPS v1 ABI; the USB serial projection publishes `serial.port` v1. Other advertised capabilities retain version zero until they have real versioned contracts. Device identity and generation, state, provider priority, and version determine selection, not an app-supplied implementation name. The resolver picks the available compatible device with the lowest priority number, using registry slot order for ties. Provider activation and signed metadata remain separate work.

## Pre-ELF transactional binding

After the SD VFS is available, `launch_elf_app()` calls the trusted firmware-only `native_app_capabilities_ready()` and `native_app_capabilities_bind()` **before symbol registration or `dlopen`**. Each pass validates the sidecar, filename and firmware floor. Required GNSS availability is probed without starting UART or mapping the GNSS ELF; USB reconciliation observes already-published host sessions without starting host hardware. The second pass re-reads and resolves the declarations, then binds each mandatory requirement to a generation-qualified device using a bounded `Mode::Dependency` registry lease. Every lease is tagged with the current, authenticated `ExecutionContext` invocation ID. If any required provider disappears, fails version negotiation, or exceeds the shared 16-lease table, earlier claims are rolled back and ELF mapping is refused. Valid leases are tracked as the context's `Dependencies` resource and reclaimed deterministically on application exit; removal revokes the precise original generation, never a visually identical replug. Optional declarations are not claimed or used for rejection.

`Mode::Dependency` is **not a hardware-access lease**: it cannot authorize I/O, is never exported to ELF code, and does not conflict with a provider's separate shared/exclusive physical access lease. The GNSS/USB adapters continue to obtain and check their existing access leases. A requirement only proves a compatible dependency was bound at launch; providers can still disconnect or fail afterward. Consumers must handle revocation and device events. No callback pointer or ELF-owned allocation enters the registry.

The maximum required dependency count is six; existing app manifests declaring no dependencies continue through the legacy path. If there is no active execution context, a mandatory dependency cannot be bound even if the inventory shows a matching device. This remains single-owner-task reconciliation and is not an ELF-thread-safe global registry.

## Security boundaries

Manifest claims are not grants. This implementation does not provide trusted user permission UI, cryptographic manifest/ELF authenticity, isolation between native code, general public semantic acquire/release APIs, driver activation or dependency groups. The runtime-only version declarations are trusted only because current adapters are compiled-in firmware; later driver-provided claims must be derived from authenticated manifests and verified provider contracts. An installed driver is not necessarily activated; the USB host currently has to enumerate before mandatory `serial.port` can be satisfied. Do not add it as a mandatory requirement to an app whose own startup must first power on that host.

## Verification and remaining work

`test/resources/app_capability_requirements_test.cpp` tests generic provider metadata, API floors, priority and unavailable states. `test/resources/app_dependency_bindings_test.cpp` tests atomic rollback (including lease-table exhaustion), identity generation, wrong-owner denial and compatibility with a simultaneous exclusive physical lease. `test/native_apps/test_capability_manifest.py` verifies manifest-build validation; the isolated ELF loader test asserts preflight and binding failures execute zero `dlopen` calls and allow later retry. Tests run under address/undefined-behavior sanitizers where applicable. Firmware CI builds two boards and validates native ELF and driver packaging. Physical USB/GNSS acceptance has not been performed.

Next: trusted permission UI and a public semantic capability acquire API with rights-checked object handles; provider activation before preflight, signed package identity and manifest authenticity, optional runtime availability querying, springboard compatibility UI, and robust handling of lost mandatory dependencies during an active app. Full pre-unload shutdown of all other legacy stream/provider resources is a separate execution-context lifecycle migration; this dependency resource currently participates in the existing native-stream context teardown.
