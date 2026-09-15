# Runtime driver migration progress

This tracks implementation of [the architecture](RUNTIME_DRIVER_ARCHITECTURE.md).
The architecture remains the target; the entries below describe the current code.

## First slice: compiled-in network provider

Radio operations previously embedded in activities now live in
`src/providers/network/Esp32NetworkProvider.cpp`. The runtime binds that provider
in `src/runtime/network/NetworkService.cpp`. The application-facing header has
no Arduino, ESP-IDF, or board dependencies.

Two independently versioned internal contracts establish the separation:

| Contract | Consumer operations |
| --- | --- |
| Generic network interface, API 1 | Connection state, assigned address, shutdown |
| Wi-Fi control, API 1 | Scan, connect, cancel, station/AP configuration, MAC, SSID, signal strength |

These are C++ contracts for compiled firmware, **not a published driver ELF ABI**.
Lookup rejects unsupported API versions. The binding is fixed for firmware
lifetime, so convenience calls assume the compiled-in version-1 provider exists.
It is not yet a capability registry or dynamic provider selection mechanism.

### Migrated consumers

- Wi-Fi selection: all direct radio operations, including the ESP32-specific
  station restart, scan invocation, SDK credential clearing, and hostname setup.
- File transfer: station/AP setup, signal and connection checks, radio shutdown.
- Calibre connection: connection/address/SSID lookup and radio shutdown.
- OPDS browser: generic network readiness and shutdown.
- KOReader sync: generic connectivity and shutdown.
- Firmware update activity: station setup and shutdown.
- Native network bridge: its existing `wifi_connected` ABI slot forwards to the
  generic network service; its ABI and exported symbols remain unchanged.

Credential UI, sorting/deduplication, retry timing, navigation, and rendering stay
in the activities. The provider preserves the scan reset sequence and connection
setup. Association and address readiness remain distinct: OPDS still requires
both. Wi-Fi selection releases scan results without disconnecting its parent.

Shutdown stops clock synchronization, disconnects an active AP when needed,
disconnects the station without erasing saved credentials, waits 100 ms, turns
the radio off, and waits another 100 ms. All migrated parent activities now use
this common sequence; shutdown formerly used waits of 0–100 ms depending on the
activity. Protocol services are still stopped by their owning activities first.

### Validation

`bash test/run_runtime_network_test.sh` compiles the real provider and runtime
service against a fake Wi-Fi backend. It checks version rejection, state mapping,
DHCP readiness and stale addresses, scan status translation and bounds, station
restart ordering, credential/hostname setup, child scan cleanup, AP addressing,
and shutdown order. It also guards against direct Wi-Fi calls returning to
activities. CI runs this test and builds both firmware targets.

On-device verification is still required for scan reliability, authenticated and
open connections, parent/child navigation, AP file transfer, and reconnects.

## GPS driver ELF pathway

The next implemented slice is a real installable GPS provider. See
[GPS_DRIVER.md](GPS_DRIVER.md) for the ABI, runtime load/unload path, kernel
resource ownership, independent build, package installation, tests, and release
cycle. GPS parsing is no longer compiled into the firmware.

## Remaining architecture work

- Phase A registry, logical aliases, handles/reference counts, capability-loss
  events, manifest requirements and launch gating are not implemented here.
- Network shutdown retains the existing single-owner activity behavior. Shared
  consumer leases and serialized resource arbitration must precede concurrent
  provider use or unloading.
- ESP-IDF HTTP/TLS, DNS/mDNS, web services, watchdog calls, other native bridges,
  storage/HAL access, and other hardware families still need migration.
- VFS/mounts, general provider discovery, device profiles, dependency resolution,
  and driverless recovery remain subsequent phases. The existing compiled-in
  ESP32 networking remains available; this slice does not add recovery commands.
