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

The firmware-owned `DeviceRegistry` copies a version per declared live capability from the trusted `Descriptor.capabilityApiVersions` array into each `DeviceInfo`. A missing version is represented as zero and **cannot** satisfy a version-constrained mandatory requirement. Independently installed provider packages are resolved through the verified installed-provider inventory instead of being required to appear in `DeviceRegistry` before their lazy ELF has been activated. Their version comes from the integrity-checked `provider-abi.v1` profile plus the verified dependency graph, never from a hard-coded provider name. The onboard GNSS adapter currently publishes `location.position` at the existing GPS v1 ABI; the USB serial projection publishes `serial.port` v1. Device identity/generation and provider package identity remain separate selection mechanisms: live registry devices use state/priority/version, while installed providers use the verified capability/API profile and dependency graph.

## Pre-ELF transactional binding

After the SD VFS is available, `launch_elf_app()` calls trusted firmware-only `native_app_capabilities_ready()` and `native_app_capabilities_bind()` **before symbol registration or `dlopen`**. Each pass validates sidecar, filename and firmware floor. Required GNSS availability is probed without starting UART; USB reconciliation observes already-published host sessions. For a mandatory capability already represented by an available compatible `DeviceRegistry` entry, the second pass binds the same generation-qualified `Mode::Dependency` lease as before. If no compatible live registry entry exists, preflight snapshots the fully verified installed provider packages and accepts only a dependency-complete `provider-abi.v1` version meeting the declared minimum. Binding then lazily admits and activates that installed provider/dependency chain and holds a provider-graph grant for the invocation before the application ELF maps. This prevents an installed provider such as `input.touch.raw` API 1 from being misreported as `Missing` merely because its ELF has not yet been activated.

Both live dependency leases and installed-provider grants belong to the current execution-context lifetime and are rolled back transactionally on later binding failure. They are reclaimed before unload; a provider that cannot quiesce remains retained/fail-closed rather than leaving an unmapped callback. Optional declarations are not launch claims and never reject startup.

`Mode::Dependency` is **not a hardware-access lease**: it cannot authorize I/O, is never exported to ELF code, and does not conflict with a provider's separate shared/exclusive physical access lease. GNSS/USB adapters continue their existing physical session paths. A requirement only proves a compatible dependency was bound at launch; providers can still disconnect or fail afterward. Consumers must handle revocation and device events. No callback pointer or ELF-owned allocation enters the registry.

The maximum required dependency count is six; existing app manifests declaring no dependencies continue through the legacy path. If there is no active execution context, a mandatory dependency cannot be bound even when inventory shows a matching device. This remains single-owner-task reconciliation and is not an ELF-thread-safe global registry.

## Authorization is separate

The new [Device Capability Access API v2](DEVICE_CAPABILITY_ACCESS_API.md) provides a public semantic `acquire/validate/release` interface protected by a separate firmware-only grant table: owner invocation, exact device generation, capability and read/write/configure rights. It defaults to denial; manifest dependency binding does not add a grant or create a public access handle. Access handles are backed by distinct tracked nonblocking leases, not by exposing the loader's mandatory dependency lease. An application cannot grant itself rights. The trusted UI that issues grants and the provider-side checks that consume v2 leases **are not yet wired**, so neither v2 nor this launch gate should be described as fully enforcing permission on legacy GPS/USB APIs.

Live registry version declarations are trusted only when published by firmware-owned adapters. Installed driver/provider claims are derived from integrity-verified package contents, the exact `provider-abi.v1` profile, and a verified dependency graph before activation. The current design still does not provide process isolation for native code. A hardware-backed provider can satisfy version preflight from installed metadata but still fail the binding phase if its actual `start()` cannot bring the hardware online; that failure correctly prevents the required app from mapping.

## Verification and remaining work

`test/resources/app_capability_requirements_test.cpp` exercises version metadata, floors, priority and offline states. `test/resources/app_dependency_bindings_test.cpp` tests rollback/lease-table exhaustion, generation, owner denial and exclusive physical lease coexistence. `test/native_apps/test_capability_manifest.py` validates build declarations; isolated ELF launcher tests verify preflight and bind denial map no ELF and permit retries. V2 authority, C layout and public bridge regressions live in `test/resources/capability_access_test.cpp`, `device_api_v2_abi_test.c` and `device_bridge_v2_test.cpp`. Host sanitizers and both firmware CI targets check integration; physical USB/GNSS acceptance has not been performed.

Remaining: trusted permission/device picker and stable package identity; provider-side authorization enforcement with explicit v2 access handles; optional runtime availability querying, springboard compatibility UI, handling lost mandatory dependencies in an active app, and full legacy resource shutdown ordering. Mandatory installed-provider activation/version validation now occurs before application mapping and participates in execution-context dependency teardown.
