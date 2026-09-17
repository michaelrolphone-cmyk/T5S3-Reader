# Provider-side capability authorization integration

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [GNSS Registry Integration](STREAM_GNSS_REGISTRY_INTEGRATION.md).

**Implementation state:** Draft PR #72 is stacked on draft PR #67 so it can use the real GNSS semantic bridge without duplicating the location implementation. The GNSS semantic `subscribe()` and `poll()` paths use the common authorization validator. This is **not** complete hardware isolation or a sandbox: legacy `T5GpsApi`, `T5UsbApi`, direct USB streams and `T5SerialPortApi` v1 retain their existing access behavior; delegated GNSS record handles are not yet independently bound to consent revocation.

## Runtime contract

A provider must validate permission immediately before an operation rather than treating discovery, a manifest dependency or a successful prior authorization check as a standing grant. `ProviderAuthorization::semantic(access, devices, context, issued, device, capability, rights)` requires:

- The real currently running execution context is the explicitly supplied owner.
- The issued handle exists in `CapabilityAccess`, represents a trusted grant rather than an arbitrary registry lease, and includes all requested rights.
- The handle's generation-qualified device equals the provider's exact device handle.
- The registry-backed issued lease is a non-physical `Dependency` grant with the exact expected semantic capability.
- Permission remains valid following any device removal, explicit revocation or handle release. A stale or replaced device cannot inherit a previous grant.

`ProviderAuthorization::io(...)` additionally requires a *separate* actual physical registry lease for the same context/device/capability. Dependency-mode leases never qualify. READ may use a provider-declared Shared or Exclusive physical mode; WRITE or CONFIGURE requires Exclusive. This helper does not acquire or release a bus, prompt the user, or pass app-controlled pointers to a driver ELF. The resource manager remains responsible for bus contention, cleanup and time-of-check/time-of-use serialization.

## GNSS provider integration — implemented on this branch

`src/native/NativeLocationBridge.cpp` derives the expected physical device generation from the firmware registry, then calls `ProviderAuthorization::semantic` for an issued `location.position` READ permission, using the actual invocation. It never trusts an app-supplied context or hardware identity. When a subscription starts the GPS provider, permission and device generation are checked again after driver startup. After successful low-level subscription, the bridge checks once more before returning handles; if permission disappeared during the handoff, it unsubscribes, zeros outputs and stops the driver if this call started it. These checks prevent a revoked grant during synchronous startup from escaping as an authorized subscription. GPS source hardware is still exclusively managed by `GpsDriverRuntime`, and the producer/record queue remain in the shared runtime stream registry.

**Boundary:** These handoff and `poll()` checks do not automatically revoke an already returned `t5_stream_t`: a caller may read accepted records through `record_read()` without calling `location->poll()` again. Stream-handle authorization and revocation below remain mandatory before any full end-to-end security claim.

## Remaining migration and acceptance work

1. **Serial:** Add a backward-compatible, authorization-bearing serial acquisition API. Resolve a concrete device generation before requesting consent. USB discovery/activation must be reconciled with pre-host enumeration: do not require a mandatory `serial.port` manifest dependency when no device can be observed before activating the host. Validate issued READ/WRITE/CONFIGURE rights and exclusive physical reservation before configure, control-line or data I/O.
2. **Stream delegation:** Bind protected RX/TX and GNSS record streams to authorization handles. Revoking or releasing an authorization must close or disable its delegated streams and connected pipes, including copied or connected handles; checking only `poll()` is insufficient. Make any retained accepted-record draining after hardware loss an explicit security policy rather than an accidental exposure after user revocation.
3. **Legacy migration:** Migrate Serial Monitor, Flasher and GPS apps to protected semantic interfaces, then remove or explicitly restrict broad legacy routes once the USB/GNSS hardware acceptance matrix passes. Do not silently break published v1 ABI or claim ELF memory isolation.

## Automated verification

`test/run_provider_authorization_test.sh`, invoked alongside the GNSS tests by `test/run_driver_test.sh`, runs real-registry authorization and physical-mode tests under address/undefined-behavior sanitizers. They cover capability/owner/device/right identity, manifest dependency spoofing, distinct physical ownership, permission release/revocation, hardware release, replacement generations, stopped contexts and exclusive WRITE/CONFIGURE. `test/streams/location_native_bridge_test.cpp` links the actual GNSS bridge and injects revocation both **during driver start** and **after low-level subscription**; both must return DENIED, clear outputs and release temporary driver/stream resources. All host and firmware CI results must be recorded against the exact stacked-branch head; the previous separate-branch green runs do not validate the newly combined implementation.
