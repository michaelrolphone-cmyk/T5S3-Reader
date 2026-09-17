# Provider-side capability authorization integration

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Device Capability Access API](DEVICE_CAPABILITY_ACCESS_API.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [GNSS Registry Integration](STREAM_GNSS_REGISTRY_INTEGRATION.md).

**Implementation state:** The authorization foundation and dependent GNSS provider/stream integration are in the **same draft PR #67 targeting `master`**. This is not complete hardware isolation or a sandbox: legacy `T5GpsApi`, `T5UsbApi`, direct USB streams and `T5SerialPortApi` v1 retain their existing access behavior. Physical acceptance is pending.

## Runtime contract

A provider validates permission immediately before an operation rather than treating discovery, a manifest dependency or an earlier authorization check as a standing grant. `ProviderAuthorization::semantic(access, devices, context, issued, device, capability, rights)` requires:

- The real currently running execution context is the explicitly supplied owner.
- The issued handle exists in `CapabilityAccess`, represents a trusted grant rather than an arbitrary registry lease, and includes all requested rights.
- The generation-qualified device equals the provider's exact device handle.
- The issued lease is a non-physical `Dependency` grant with the exact expected semantic capability.
- Permission is still valid following removal, revocation or handle release; a replaced device cannot inherit a previous grant.

`ProviderAuthorization::io(...)` additionally requires a *separate* actual physical registry lease for the same context/device/capability. Dependency-mode leases never qualify. READ may use Shared or Exclusive physical mode; WRITE or CONFIGURE requires Exclusive. This helper does not acquire a bus, prompt the user, or pass app-controlled pointers to a driver ELF. The resource manager remains responsible for contention, cleanup and time-of-check/time-of-use serialization.

## GNSS semantic and delegated-stream enforcement

`src/native/NativeLocationBridge.cpp` obtains the expected physical generation from the firmware registry and calls `ProviderAuthorization::semantic` for an issued `location.position` READ grant and actual invocation. It rechecks permission after provider startup and again after subscription, rolling back the temporary stream/driver if consent disappears during either handoff.

The bridge passes the exact issued permission handle into `nativeGnssSubscribe`. Under the **existing stream registry mutex**, `GnssStreamAuthority` binds subscriber owner, device generation, issued consent, subscription token and concrete stream handle. An unissued dependency or raw hardware lease cannot be bound. `NativeStreamBridge.cpp` rechecks the bound permission before generic byte reads/writes, `record_read`, stream metadata and record metadata. On denial, it unsubscribes, closes and destroys the queue, releases the subscriber's physical device grant and clears the authorization binding; the attempted read returns `T5_STREAM_DENIED` with zero output length. A newly issued permission cannot reactivate a stream whose original grant was released. Generic stream close and semantic unsubscribe both clear this state, and execution-context cleanup removes any remainder.

**Delegation policy:** Generic `connect()` rejects GNSS-bound streams, even while authorized. A generic pipe could otherwise retain already-copied fixes in an unprotected destination after revocation. Do not enable GNSS-to-file/recorder pipes until the stream/pipe framework propagates capability authorization to every derived destination, and defines secure purge/cancel behavior. A record that the application already read before revocation cannot be retroactively recovered. The GNSS data-plane's previously accepted-record drain after *physical* disconnect remains a property of its internal queue; the public bridge deliberately denies it when the issued grant becomes invalid, including generation loss. Explicit revocation is never a drain authorization.

This protection applies to the **GNSS semantic stream path only**. It does not secure generic streams opened through legacy raw USB or unrelated provider APIs, nor does it provide ELF memory isolation. `CapabilityAccess` policy mutation and permission-checked raw calls currently execute on the single claiming app task; the pipe scheduler never touches protected GNSS streams because connection is denied. A future independently concurrent service must serialize permission mutation and protected I/O under a shared policy lock.

## Remaining migration and acceptance work

1. **Serial:** Add a backward-compatible, authorization-bearing serial acquisition API. Resolve a concrete device generation before requesting consent. USB discovery/activation must account for pre-host enumeration: do not require a mandatory `serial.port` app-manifest dependency when no device can be observed before activating the host. Validate issued READ/WRITE/CONFIGURE rights and exclusive physical reservation before configure, control-line or data I/O.
2. **Delegation:** Implement authorization-taint propagation to sinks and pipes before enabling protected GNSS source connections. Extend analogous protection to USB RX/TX after serial authorization is implemented.
3. **Legacy migration:** Migrate Serial Monitor, Flasher and GPS apps to protected semantic APIs, then remove or restrict broad legacy routes when USB/GNSS hardware acceptance passes. Do not silently break a published v1 ABI or claim an ELF sandbox.

## Automated verification

`test/run_provider_authorization_test.sh`, invoked by `test/run_driver_test.sh`, runs real-registry authorization, physical-mode and `gnss_stream_authority_test.cpp` under address/undefined-behavior sanitizers. The stream test checks exact issued consent, spoofed dependencies, unrelated capability, owner, new grant not reviving an old stream, explicit revocation, device-generation replacement and cleanup. `test/streams/location_native_bridge_test.cpp` links the actual GNSS semantic bridge and injects revocation during driver startup and low-level subscription, asserting denial, cleared handles and rollback. Both firmware targets compile the actual raw stream bridge. Hardware and broader legacy-route security acceptance are still separate gates; record CI results against the exact final commit, not an earlier successful run.
