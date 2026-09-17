# RiscRTE GNSS Provider — `gps-nmea`

> **Specification authority:** Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), [HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), the roadmap's device/sensor sections, and [RUNTIME_DRIVER_ARCHITECTURE.md](RUNTIME_DRIVER_ARCHITECTURE.md). This document describes a **partially modular current implementation**, not permission to implement GNSS or UART hardware in RiscRTE core.

## Canonical target: the ELF owns the hardware implementation

Consumers request semantic `location.position`, `location.altitude`, `location.velocity`, `location.heading`, `location.time`, `location.accuracy` and `location.satellites`; they must not know the GNSS driver, UART, board identity or transport. The GNSS provider ELF owns receiver power requests, hardware acquisition through an independently installed bus/controller provider (or narrow generic CPU/OS port primitives), receiver initialization, baud probing, NMEA parsing, sampling, status, failures and recovery. A UART provider ELF owns UART hardware operations, shared-port arbitration and physical session teardown; a board power provider ELF owns the physical rail. The generic RiscRTE core resolves arbitrary capability strings, handles generic permissions/leases, and supervises ELF lifecycles; it MUST NOT claim UART, sequence GPS rails, register a hard-coded GPS adapter, poll GPS, or parse NMEA.

Example:

```text
location_app.elf -> location.position
                       ^
                  gps_nmea.elf
                    | requires serial.port or uart.controller capability
                    | optional requires board.power.gnss
                    v
             UART/power provider ELFs -> hardware

RiscRTE core: generic manifests / opaque handles / rights / streams / lifecycle only.
```

`position.gnss`, `T5GpsApi`, `t5_driver_get`, `.t5driver.zip` and `gps-nmea` are current compatibility/implementation identifiers. Preserve them while actual deployed code depends on them but do not hard-code them into future core behavior. A driver-specific compatibility API is not proof of general provider activation.

## CURRENT/LEGACY, NONCOMPLIANT implementation

The GNSS ELF contains real receiver logic and parsing, with no compiled-in GPS parser fallback or TinyGPSPlus firmware dependency. However, firmware currently retains the GPS-specific compatibility facade/adapter and performs UART endpoint claiming and power/resource cleanup. This portion **does not meet** the hardware-agnostic target. Legacy GNSS registry publication, UART/rail binding and firmware callbacks must migrate to provider-side discovery, dependencies, hardware acquisition and lifecycle. The original `gps-nmea` implementation proves independent ELF build/package/load and a substantial hardware algorithm, but not complete elimination of firmware hardware management.

### Build independently

Use the firmware's ESP32-S3 Xtensa compiler (GCC 8.4.0, 2021r2-patch5):

```sh
python scripts/build_driver.py
```

`--cc /path/to/xtensa-esp32s3-elf-gcc` or `NATIVE_DRIVER_CC` selects an explicit compiler. Otherwise the script checks PATH and PlatformIO's package directory; no firmware compilation/linked image is needed. Outputs: `dist/drivers/gps-nmea/driver.elf`, `dist/drivers/gps-nmea/manifest.json` (ELF size/SHA-256), and compatibility package `dist/drivers/gps-nmea-1.0.0.t5driver.zip`. Build validation checks ELF architecture, `t5_driver_get`, imports and linked software double precision. Existing kernel primitives are supplied through a function table and not exported as app-global symbols.

### Install and update (current behavior)

Firmware supporting driver ABI 1 is required. With the device off and SD card mounted on a computer:

```sh
python scripts/install_driver.py dist/drivers/gps-nmea-1.0.0.t5driver.zip --sd-root /path/to/sd-card
```

The installer checks ZIP members, size, ABI/manifest and hashes; stages/flushes before renaming into `/Drivers/gps-nmea/manifest.json` and `/Drivers/gps-nmea/driver.elf`. The former directory may be retained as `/Drivers/.gps-nmea.previous` for offline rollback; remove the backup after verifying and before another update. Alternatively extract the two ZIP entries to `/Drivers/gps-nmea` offline, not `/Apps`, and do not flash an ELF as firmware. This description is of the legacy installer, not a requirement that future packages need desktop installation.

### Runtime path (current behavior, not target)

1. The GPS app uses compatibility `T5GpsApi`.
2. Firmware validates manifest, primitive ABI, architecture, file identity, bounded size and SHA-256.
3. **Legacy noncompliant:** firmware claims the configured UART endpoint and handles associated ownership/power.
4. `dlopen` loads `/sd/Drivers/gps-nmea/driver.elf`; `dlsym` resolves `t5_driver_get` ABI 1.
5. Firmware checks `position.gnss` compatibility and invokes provider `start` with its bounded kernel I/O table.
6. The ELF powers/probes the receiver at 9600/38400 baud, parses checksummed NMEA GGA/RMC and returns coordinates/status.
7. **Legacy noncompliant:** firmware's GPS-specific stop path clears callable pointers, unloads driver and releases the UART/rail. Failed unload retains the module handle.

Apps do not retain ELF code pointers. An installed ELF does not automatically power the receiver at boot. Checksummed NMEA means receiver detection, not a position fix. Current implementation uses a 1600 ms baud-probe window, 5000 ms fix freshness, 1024 bytes/read limit, immediate invalidation of bad navigation status, and GGA/RMC for valid two-character talker prefixes. Current board binding supplies UART pins and shared GPS/LoRa rail; EPD47 reports the endpoint unavailable. Firmware's GPS/LoRa shared-rail ownership bits and claiming-task checks are legacy mechanisms that must migrate to independent provider capability dependencies.

### Build/release cycle

Pull requests build both firmware targets, build/validate the ELF, run driver tests and attach the ZIP to firmware artifacts; firmware releases include the driver package. The `GPS driver package` workflow can build without firmware. `driver-gps-nmea-v1.0.0` explicitly tags/tests/releases only the driver and must match `Drivers/gps_nmea/manifest.json`. This documentation does not publish any tag or release.

### Verification of current behavior

```sh
bash test/run_driver_test.sh
python scripts/build_driver.py
bash test/run_native_app_test.sh dist/drivers/gps-nmea/driver.elf
```

Host tests cover loads/ABI negotiation/start, reload, cleanup, baud switching, fragmented sentences, checksums, stale/invalid fixes, wrap, noise, shared rail, packaging and rollback. On-device UART reception, PSRAM execution, satellite fixes, repeated entry/exit and GPS/LoRa rail behavior still require physical verification.

## Migration requirements and acceptance

Replace firmware's `GpsDriverRuntime`/`T5GpsApi` hardware-specific activation with generic capability registration/dispatch. Package real UART and power-controller operations into provider ELF(s), remove board-specific GPS code and rail ownership from core, have GNSS ELF declare and acquire dependency capabilities, perform driver-originated device publication and stream observations, and bind `location.*` semantically. Preserve compatibility names only behind explicit adapters that do not perform hardware I/O; retire those adapters when consumers migrate.

The provider must remain functional after installing its ELFs/profiles without rebuilding firmware. Removing the GNSS ELF must remove location capabilities; removing its UART/power dependency must cause a clear failed dependency instead of triggering a hidden firmware implementation. Test concurrent GPS/LoRa rail use, UART conflicts, missing/corrupt drivers, app exit, provider cancellation, actual hardware loss and full teardown. Signed trust, generic dependency resolution and hardware acceptance remain separate requirements. Hash integrity alone does not authenticate a publisher.
