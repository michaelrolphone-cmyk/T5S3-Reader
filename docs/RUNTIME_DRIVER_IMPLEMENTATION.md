# RiscRTE Runtime Provider/Driver Implementation State

## Target platform contract

The normative driver/provider design is `RUNTIME_DRIVER_ARCHITECTURE.md`, governed by `RISCRTE_PLATFORM_SPEC.md` and the device/capability/resource model in `PLATFORM_CAPABILITY_ROADMAP.md`. RiscRTE applications and services should request semantic capabilities; the resolver/device registry selects providers; runtime-owned resources mediate hardware; installable drivers/providers remain independently replaceable where practical.

This file is not a competing architecture. It records the migration state of existing code. New work should close the gaps below rather than institutionalize them.

## Current implementation

### Compiled-in network provider precursor

Radio operations previously embedded in activities now live in `src/providers/network/Esp32NetworkProvider.cpp`, bound by `src/runtime/network/NetworkService.cpp`. The application-facing header has no Arduino, ESP-IDF or board dependencies.

Two independently versioned internal C++ contracts currently separate generic network state/address/shutdown from Wi-Fi scan/connect/configuration/MAC/SSID/signal operations. These are compiled-firmware contracts, not a published driver ELF ABI. Binding is still fixed for firmware lifetime and is therefore a precursor to, not an implementation of, general capability resolution/dynamic provider selection.

Migrated consumers include Wi-Fi selection, file transfer, Calibre, OPDS, KOReader sync, firmware update and the compatibility native network bridge. Credential UI, retry/navigation/rendering policy remains above the provider. Common shutdown now centralizes clock-sync stop, disconnect and radio-off sequencing.

`bash test/run_runtime_network_test.sh` validates provider/service behavior and guards against direct Wi-Fi calls returning to migrated activities. Physical acceptance remains required for scan/connect/AP/reconnect behavior.

### Installable GNSS provider pathway

The first actual installable hardware provider is documented in `GPS_DRIVER.md`. GPS parsing is no longer compiled into firmware. The existing `gps-nmea` ELF, `position.gnss` capability and `T5GpsApi` facade prove the independent build/package/load/unload pathway while remaining a compatibility slice pending the roadmap's general resolver and semantic `location.*` ABI.

### Unified Device Registry and context-owned leases: GNSS and USB vertical slices

`src/runtime/capabilities/DeviceRegistry.h` provides a bounded, firmware-owned, transport-independent inventory. It copies device/provider identity and semantic capability names into fixed records; discovery state, transport, capability metadata, priority and opaque generation-safe device handles can be queried without loading an ELF. Its resolver acquires shared or exclusive capability leases under a nonzero application invocation identity. A wrong owner cannot inspect or release another invocation's lease. Removal or loss of availability revokes leases; slot reuse changes the handle generation. `releaseOwner()` provides deterministic invocation-wide revocation. The registry is firmware-internal and currently serialized by the runtime owner task, **not** an app-facing SDK ABI or a complete asynchronous event-driven discovery service.

The onboard GNSS adapter publishes a UART logical device with `location.position`, `location.altitude`, `location.time`, `location.accuracy` and `location.satellites`. Starting its existing compatibility API resolves an invocation-owned `location.position` lease before claiming UART and loading the driver ELF. A firmware-only `ExecutionContext` cleanup callback unloads the provider, releases UART/power and revokes leases before the application ELF is unloaded. Explicit stop untracks the callback; failed start/read unwinds ownership and marks the device failed. Existing application and GPS ELF ABIs are unchanged.

`src/runtime/capabilities/UsbSerialProjection.h` connects the legacy lock-protected USB serial enumeration snapshot to the same inventory. USB host callbacks publish only into `NativeUsbDevices::Registry`; the native app owner task reconciles snapshots in `NativeSerialPortBridge.cpp`. A bound USB serial interface is a semantic USB device with `serial.port` and `serial.host`. Disconnect, interface rebinding or session epoch changes replace its generation-safe identity and revoke its leases. VID/PID/product are **not** permanent or globally unique physical identity. The existing `T5SerialPortApi` remains a compatibility surface.

`NativeSerialPortBridge.cpp` and `NativeStreamBridge.cpp` now share one owner-task physical USB session reservation and one exclusive `serial.port` capability lease for **both** semantic serial sessions and legacy direct USB streams. A session can be reserved during asynchronous host enumeration; on binding, only the original consumer's execution context acquires the physical lease. The direct stream's pipe-scheduler callbacks may not perform I/O until the owner task has acquired this lease; they never mutate the device registry. The direct stream's owner-task open/read/write/pipe-connect operations reconcile enumeration. A stale stream epoch reconciles device loss and revokes its old lease without claiming an identical replacement. A failed duplicate direct open must not release the active stream's lease. Explicit close and invocation teardown release the direct lease, while serial teardown releases its serial lease and host. Existing app/driver ELF ABIs are unchanged.

`test/resources/device_registry_test.cpp`, `test/resources/usb_serial_projection_test.cpp` and `test/streams/usb_direct_ownership_test.cpp` exercise identity, priority, conflicts, cross-owner denial, stale-generation rejection, app cleanup, USB detach/replug, direct-versus-serial exclusivity, duplicate-open ownership and safe stale-stream rejection. The production USB semantic bridge and original stream test remain in the host suite. These tests do **not** prove physical USB enumeration, cross-core timing, UART/PSRAM/power behavior; those require on-device verification.

## Required convergence with the roadmap

The two initial live adapters are GNSS and USB serial. USB projection currently occurs during native application owner-task operations, **not** through a global device-manager event loop. The USB host still has its existing lifecycle and compatibility snapshot; device registration is not evidence that its installable driver was activated or tested. Physical USB VBUS lifetime, multiple interfaces, hub discovery, BLE inventories, device lifecycle event fanout and independent driver/provider activation remain outstanding. The remaining `usbOpen` flag is a compatibility guard for stream slot occupancy; it must not be treated as the physical authorization source. This slice consolidates the known direct/serial paths' exclusive capability lease, not every future USB class or kernel client.

`GpsDriverRuntime` is still the concrete compatibility adapter beneath its semantic device record; generic provider activation, manifest-driven capability declarations, automatic provider selection, dependency graphs, framework-wide events and public app-facing semantic location APIs remain outstanding. Do not mistake a registered device for an installed, activated or verified driver.

Next: verify physical direct/serial USB ownership and repeated disconnect/reconnect on-device; promote the registry to a runtime-wide discovery service with bounded event delivery; add USB classes and BLE transport adapters; generalize resource cleanup and manifest capability requirements/launch gating; and expose semantic location without breaking deployed `T5GpsApi`. Network should migrate from fixed compiled binding to the same provider/capability model where feasible.

Shared I2C/SPI/GPIO buses must become runtime-owned managers before arbitrary installable drivers receive bus access. USB, BLE, UART and future transports should register devices/capabilities through the same registry rather than create parallel inventories. Capability-loss/device lifecycle events should flow through the common event fabric. All transport discovery callbacks must marshal registry mutations onto its owner task until synchronization is explicitly implemented.

HTTP/TLS, DNS/mDNS and higher network facilities belong in reusable RiscRTE networking services above the Wi-Fi provider rather than individual applications. VFS/mounts, provider discovery, device profiles, dependency resolution, signed packages and driverless recovery remain roadmap work.

## Legacy/current-state rule

Source paths, ABI symbols, package names and concrete provider names in this document are intentionally preserved because they identify code that exists today. Conceptual references use RiscRTE terminology. If this implementation-state document conflicts with the architecture/roadmap, the architecture governs new work while this file remains evidence of what must be migrated.
