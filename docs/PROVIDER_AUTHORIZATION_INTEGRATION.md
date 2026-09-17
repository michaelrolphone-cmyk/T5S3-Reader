# Provider-side capability authorization integration

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), and [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

**Implementation state:** The `ProviderAuthorization` firmware-only helper and host tests exist in draft PR #72. This is a *validation primitive*, not a completed hardware sandbox. Legacy `T5GpsApi`, `T5UsbApi`, direct USB streams and `T5SerialPortApi` v1 still have their documented access behavior and must not be described as authorization-protected.

## Runtime contract

A provider must validate permission immediately before an operation rather than treating discovery, a manifest dependency or a successful prior authorization check as a standing grant. `ProviderAuthorization::semantic(access, devices, context, issued, device, capability, rights)` requires:

- The real currently running execution context is the explicitly supplied owner.
- The issued handle exists in `CapabilityAccess`, represents a trusted grant rather than an arbitrary registry lease, and includes all requested rights.
- The handle's generation-qualified device equals the provider's exact device handle.
- The registry-backed issued lease is a non-physical `Dependency` grant with the exact expected semantic capability.
- Permission remains valid following any device removal, explicit revocation or handle release. A stale or replaced device cannot inherit a previous grant.

`ProviderAuthorization::io(...)` additionally requires a *separate* actual physical registry lease for the same context/device/capability. Dependency-mode leases never qualify. Read operations may use a provider-declared Shared or Exclusive physical mode; WRITE or CONFIGURE always requires Exclusive. This helper does not acquire or release a bus, prompt the user, or pass app-controlled pointers to a driver ELF. The owner-side resource manager remains responsible for contention, cleanup and time-of-check/time-of-use serialization.

## Migration sequence and acceptance requirements

1. **Location:** Replace duplicated authorization checks in the semantic `location.position` bridge with `ProviderAuthorization::semantic` after PR #67 and this PR converge. Keep the GPS driver as sole holder of its borrowed physical lease. Confirm that an issued READ handle for a different location capability, a raw source lease and a manifest dependency are denied.
2. **Serial:** Add a backward-compatible, authorization-bearing serial acquisition API. Resolve a concrete device generation before requesting consent. USB discovery/activation must be reconciled with pre-host enumeration: do not require a mandatory `serial.port` app-manifest dependency when no device can be observed before activating the host. Validate the issued READ/WRITE/CONFIGURE rights and exclusive physical reservation at the provider before configure, control-line or data I/O.
3. **Stream delegation:** Bind protected RX/TX and GNSS record streams to their authorizations. Revoking or releasing an authorization must close or disable its delegated stream handles and associated pipes, including copies made through a stream connection; checking only a provider's `poll()` method is insufficient because callers can read already-queued records through `record_read()`. Preserve the documented handling of already-accepted records after device loss only where the security policy explicitly permits it.
4. **Legacy migration:** Move Serial Monitor, Flasher and GPS apps to protected semantic APIs. Remove or explicitly restrict legacy routes only after each consumer migrates and the USB/GNSS hardware matrix passes. Do not silently break the currently published v1 ABI or claim an ELF memory sandbox. Physical validation is independent of CI.

## Automated verification

`test/run_provider_authorization_test.sh`, invoked by `test/run_driver_test.sh` in CI, runs the authorization and physical-mode test binaries with address and undefined-behavior sanitizers. These test exact capability, owner, device generation, rights, dependency spoofing, distinct physical ownership, permission release, trusted revocation, hardware release, device replacement, execution-context termination and exclusive WRITE/CONFIGURE. They do not exercise any USB/GPS peripheral or prove the legacy APIs are protected.
