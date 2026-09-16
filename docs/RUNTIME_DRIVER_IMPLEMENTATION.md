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

The first actual installable hardware provider is documented in `GPS_DRIVER.md`. GPS parsing is no longer compiled into firmware. The existing `gps-nmea` ELF, `position.gnss` capability and `T5GpsApi` facade prove the independent build/package/load/unload pathway while remaining a compatibility slice pending the roadmap's unified Device Registry and semantic `location.*` capability model.

## Required convergence with the roadmap

The next driver/runtime work should prioritize the unified Device and Peripheral Registry, semantic capability resolver/leases, unified resource ownership, manifest capability requirements and launch gating. Network should migrate from fixed compiled binding to the same provider/capability model where feasible. GNSS should migrate from the narrow compatibility capability to the common Location Framework without breaking deployed APIs during transition.

Shared I2C/SPI/GPIO buses must become runtime-owned managers before arbitrary installable drivers receive bus access. USB, BLE, UART and future transports should register devices/capabilities through the same registry rather than create parallel inventories. Capability-loss/device lifecycle events should flow through the common event fabric.

HTTP/TLS, DNS/mDNS and higher network facilities belong in reusable RiscRTE networking services above the Wi-Fi provider rather than individual applications. VFS/mounts, provider discovery, device profiles, dependency resolution, signed packages and driverless recovery remain roadmap work.

## Legacy/current-state rule

Source paths, ABI symbols, package names and concrete provider names in this document are intentionally preserved because they identify code that exists today. Conceptual references use RiscRTE terminology. If this implementation-state document conflicts with the architecture/roadmap, the architecture governs new work while this file remains evidence of what must be migrated.