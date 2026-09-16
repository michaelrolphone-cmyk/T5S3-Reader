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

### Unified Device Registry and context-owned leases: initial GNSS and USB slices

`src/runtime/capabilities/DeviceRegistry.h` provides a bounded, firmware-owned, transport-independent inventory. It copies device/provider identity and semantic capability names into fixed records; discovery state, transport, capability metadata, priority and opaque generation-safe device handles can be queried without loading an ELF. Its resolver acquires shared or exclusive capability leases under a nonzero application invocation identity. A wrong owner cannot inspect or release another invocation's lease. Removal or loss of availability revokes leases; slot reuse changes the handle generation. `releaseOwner()` provides deterministic invocation-wide revocation. The registry is firmware-internal and currently serialized by the runtime owner task, **not** an app-facing SDK ABI or a complete asynchronous event-driven discovery service.

The onboard GNSS adapter publishes a UART logical device with `location.position`, `location.altitude`, `location.time`, `location.accuracy` and `location.satellites`. Starting its existing compatibility API resolves an invocation-owned `location.position` lease before claiming UART and loading the driver ELF. A firmware-only `ExecutionContext` cleanup callback unloads the provider, releases UART/power and revokes leases before the application ELF is unloaded. Explicit stop untracks the callback; failed start/read unwinds ownership and marks the device failed. Existing application and GPS ELF ABIs are unchanged.

`src/runtime/capabilities/UsbSerialProjection.h` connects the legacy lock-protected USB serial enumeration snapshot to the same inventory. The USB host callbacks still publish only into `NativeUsbDevices::Registry`; the native app owner task reconciles snapshots in `NativeSerialPortBridge.cpp`. A bound USB serial interface becomes a semantic USB device providing `serial.port` and `serial.host`. Disconnect, interface rebinding or session epoch changes remove the previous device and revoke its leases. Identity is generation-qualified for the enumeration session: VID/PID/product are **not** represented as permanent or globally unique physical identity. USB host start is asynchronous, so a serial session can exist while enumeration is pending; its physical `serial.port` lease is acquired once a bound interface appears. The existing stream/serial execution-context cleanup releases the lease before host shutdown. The legacy `T5SerialPortApi` and the separate, pre-existing USB snapshot and provider registries remain compatibility surfaces during migration.

`test/resources/device_registry_test.cpp` and `test/resources/usb_serial_projection_test.cpp`, run by `test/run_driver_test.sh`, exercise identity/metadata, priority resolution, shared/exclusive conflicts, wrong-owner denial, removal, loss and recovery, stale-generation rejection, invocation cleanup, USB detach/replug, and product-label sanitization. Host tests do **not** prove physical USB enumeration, cross-core timing, UART/PSRAM/power behavior; those require on-device verification.

## Required convergence with the roadmap

The two initial live adapters are GNSS and USB serial. USB projection is currently triggered by native serial owner-task operations rather than by a global device-manager event loop. Other USB interfaces, BLE inventories, device/service discovery events, registry-level physical identity evidence, and globally discoverable devices before opening a serial session remain outstanding. The device registry does not yet own USB host startup; the serial compatibility subsystem still owns that lifetime. Independent serial and stream consumers must ultimately arbitrate hardware through the same capability/resource owner rather than relying solely on their existing USB busy flags.

`GpsDriverRuntime` is still the concrete compatibility adapter beneath its semantic device record; generic provider activation, manifest-driven capability declarations, automatic provider selection, dependency graphs, framework-wide events and public app-facing semantic location APIs remain outstanding. Do not mistake a registered device for an installed, activated, or verified driver.

Next: promote the registry to a runtime-wide discovery service with bounded event delivery, add provider/transport identity adapters for BLE and other USB classes, consolidate USB hardware ownership across direct stream and serial paths, generalize capability leases/cleanup for all resource classes, add manifest capability requirements and launch gating, and expose a semantic location API without breaking deployed `T5GpsApi`. Network should migrate from fixed compiled binding to the same provider/capability model where feasible.

Shared I2C/SPI/GPIO buses must become runtime-owned managers before arbitrary installable drivers receive bus access. USB, BLE, UART and future transports should register devices/capabilities through the same registry rather than create parallel inventories. Capability-loss/device lifecycle events should flow through the common event fabric. All transport discovery callbacks must marshal registry mutations onto its owner task until synchronization is explicitly implemented.

HTTP/TLS, DNS/mDNS and higher network facilities belong in reusable RiscRTE networking services above the Wi-Fi provider rather than individual applications. VFS/mounts, provider discovery, device profiles, dependency resolution, signed packages and driverless recovery remain roadmap work.

## Legacy/current-state rule

Source paths, ABI symbols, package names and concrete provider names in this document are intentionally preserved because they identify code that exists today. Conceptual references use RiscRTE terminology. If this implementation-state document conflicts with the architecture/roadmap, the architecture governs new work while this file remains evidence of what must be migrated.
