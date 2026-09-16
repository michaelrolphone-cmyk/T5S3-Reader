# RiscRTE Application Capability Requirements — Launch Gating and Dependency Bindings

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This describes the current implementation, not the entire target authorization/provider-activation model. Separate authorization behavior is specified in [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md).

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

After the SD VFS is available, `launch_elf_app()` calls trusted firmware-only `native_app_capabilities_ready()` and `native_app_capabilities_bind()` **before symbol registration or `dlopen`**. Each pass validates sidecar, filename and firmware floor. Required GNSS availability is probed without starting UART or mapping the GNSS ELF; USB reconciliation observes already-published host sessions without starting host hardware. The second pass re-reads and resolves the declarations, then binds each mandatory requirement to a generation-qualified device using a bounded `Mode::Dependency` registry lease. Every lease belongs to the current, authenticated `ExecutionContext` invocation ID. If any required provider disappears, fails version negotiation, or exhausts the shared 16-lease table, earlier claims are rolled back and ELF mapping is refused. Valid leases are tracked as the context's `Dependencies` resource and reclaimed on application exit; removal revokes the original generation, never a visually identical replug. Optional declarations are not claimed or used for rejection.

`Mode::Dependency` is **not a hardware-access lease**: it cannot authorize I/O, is never exported to ELF code, and does not conflict with a provider's separate shared/exclusive physical access lease. GNSS/USB adapters continue their existing physical session paths. A requirement only proves a compatible dependency was bound at launch; providers can still disconnect or fail afterward. Consumers must handle revocation and device events. No callback pointer or ELF-owned allocation enters the registry.

The maximum required dependency count is six; existing app manifests declaring no dependencies continue through the legacy path. If there is no active execution context, a mandatory dependency cannot be bound even when inventory shows a matching device. This remains single-owner-task reconciliation and is not an ELF-thread-safe global registry.

## Authorization is separate

The new [Device Capability Access API v2](DEVICE_CAPABILITY_ACCESS_API.md) provides a public semantic `acquire/validate/release` interface protected by a separate firmware-only grant table: owner invocation, exact device generation, capability and read/write/configure rights. It defaults to denial; manifest dependency binding does not add a grant or create a public access handle. Access handles are backed by distinct tracked nonblocking leases, not by exposing the loader's mandatory dependency lease. An application cannot grant itself rights. The trusted UI that issues grants and the provider-side checks that consume v2 leases **are not yet wired**, so neither v2 nor this launch gate should be described as fully enforcing permission on legacy GPS/USB APIs.

The runtime-only version declarations are trusted only because current adapters are compiled-in firmware; later driver-provided claims must be derived from authenticated manifests and verified provider contracts. The current design does not authenticate SD manifest/ELF bytes or isolate native code. An installed driver is not necessarily activated; the USB host currently must enumerate before mandatory `serial.port` can be satisfied. Do not make it mandatory for an app whose startup must first power that host.

## Verification and remaining work

`test/resources/app_capability_requirements_test.cpp` exercises version metadata, floors, priority and offline states. `test/resources/app_dependency_bindings_test.cpp` tests rollback/lease-table exhaustion, generation, owner denial and exclusive physical lease coexistence. `test/native_apps/test_capability_manifest.py` validates build declarations; isolated ELF launcher tests verify preflight and bind denial map no ELF and permit retries. V2 authority, C layout and public bridge regressions live in `test/resources/capability_access_test.cpp`, `device_api_v2_abi_test.c` and `device_bridge_v2_test.cpp`. Host sanitizers and both firmware CI targets check integration; physical USB/GNSS acceptance has not been performed.

Remaining: trusted permission/device picker and stable package identity; provider-side authorization enforcement with explicit v2 access handles; provider activation before preflight, authenticated manifests and packages, optional runtime availability querying, springboard compatibility UI, handling lost mandatory dependencies in an active app, and full legacy resource shutdown ordering. The current dependency resource participates in existing native-stream context teardown.
