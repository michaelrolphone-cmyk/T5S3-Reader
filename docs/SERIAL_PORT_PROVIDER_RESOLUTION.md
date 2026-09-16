# `serial.port` provider resolution — Phase 3 reference implementation

Authority: [USB Capability/Stream Reference Implementation](USB_CAPABILITY_STREAM_REFERENCE_IMPLEMENTATION.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), and [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md).

## Capability boundary

Serial Monitor and the `program.esp_rom` provider consume the unchanged `T5SerialPortApi.h` v1 API. The firmware-only `RuntimeSerial::Registry` in `src/runtime/capabilities/SerialProviderRegistry.h` resolves `serial.port` to a registered semantic provider, returns an invocation-scoped opaque public lease, and dispatches `configure`, `read_status`, `set_control_lines`, and `release` to the selected provider's *private* lease. The app never receives a USB transfer, endpoint, class-driver identifier or provider callback. Both RX and TX streams belong to the lease.

The existing USB serial implementation remains in `NativeSerialPortBridge.cpp` as the permanently registered `usb.serial` provider (priority 0). Host power, USB class drivers, device-binding epochs, revoke-on-disconnect, stream handles and resident fallback are unchanged inside that provider. Trusted firmware can register another provider through `nativeRegisterSerialProvider()` and remove it through `nativeUnregisterSerialProvider()`; these hooks are not exported to ELF applications. No UART/BLE/network implementation is claimed here, only the path by which a future implementation can register without modifying the consumer or resolver.

## Resolution and lifetime

- A zero device selector chooses the available provider with the lowest numeric priority. USB wins by default; an unavailable USB provider does not prevent an available alternate provider from being selected. A provider reporting BUSY/IO on acquisition is *not* silently bypassed, so an existing user's choice or occupied USB stream cannot be rerouted behind their back.
- A nonzero device selector is checked through providers' semantic `matches` callbacks. Unknown, stale or ambiguous IDs return `INVALID`; an unavailable selected provider returns `UNSUPPORTED`. A provider must allocate IDs in a transport-distinct namespace where practical. Ambiguity is rejected rather than guessed.
- Registration is bounded to four providers, allocates no heap, refuses duplicate or invalid identities, and is permitted only when no serial lease is active. A provider cannot unregister while its lease is active. Provider code and context must outlive registration, and registration/removal must happen on the app owner's task; the USB host's independently synchronized attach/detach publication remains unchanged.
- The resolver issues monotonic generation-safe public lease IDs independently of each provider's private lease. Invalid, released and prior-invocation leases return `CLOSED`, including after provider replacement. It validates successful provider acquisition has one private lease and two distinct nonzero streams; malformed results fail and release the private lease.
- `nativeSerialPortsEnd()` releases the selected provider's lease while app-owned stream teardown is still possible, then reclaims the remaining context resources. A non-USB lease does not run the USB provider's `serial_stop()` path. The USB provider stays registered across app invocations.

## Automated coverage

`test/streams/serial_provider_registry_test.cpp` checks priorities, explicit selection, unavailable fallback, BUSY/invalid acquisitions, ambiguous IDs, duplicate/capacity limits, private/public lease dispatch, generation-safe rejection and unload restrictions. `test/streams/serial_provider_bridge_test.cpp` compiles the actual production `NativeSerialPortBridge.cpp` against an alternate semantic provider and mocked USB; it checks USB defaults and device binding, alternate selection without USB startup, status/configuration/control dispatch, stale selectors, selected-provider release and app teardown. Both tests run under address/undefined-behavior sanitizers from `test/run_stream_test.sh`, alongside the existing USB/stream regression suite.

## Outstanding validation

Host tests and firmware builds do not establish physical parity. Capture USB OTG/VBUS timings and runtime-layer diagnostic traces, then verify supported CDC/CP210x/CH34x adapters, serial RX/TX at multiple baud rates, repeated launches, full flash/MD5/reboot, cancellation, detach/replug and retry on actual boards before removing resident fallback or declaring the entire USB reference specification complete. Universal discovery, permissions and bus managers remain outside this narrow Phase 3 resolver slice.
