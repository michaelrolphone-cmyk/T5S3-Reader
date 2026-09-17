# RiscRTE GNSS Provider — `gps-nmea`

> **Specification authority:** Read [RISCRTE_PLATFORM_SPEC.md](RISCRTE_PLATFORM_SPEC.md), Roadmap §§3–9, and [RUNTIME_DRIVER_ARCHITECTURE.md](RUNTIME_DRIVER_ARCHITECTURE.md) first. This file documents the **current first-driver implementation** and its compatibility ABI.

## Canonical target

GNSS is a provider beneath RiscRTE's transport-independent Location Framework. Consumers should ultimately request semantic capabilities such as `location.position`, `location.altitude`, `location.velocity`, `location.heading`, `location.time`, `location.accuracy`, and `location.satellites` through the capability resolver. They must not depend on `gps-nmea`, a particular UART, or a board-specific GPS implementation.

`position.gnss`, `T5GpsApi`, `t5_driver_get`, `.t5driver.zip`, and similar names below are current compatibility/implementation identifiers. Preserve them while existing code depends on them; do not use them as the naming model for new RiscRTE APIs. The current fixed GPS binding is a migration seed for the unified Device Registry, capability resolver, runtime-owned bus/UART resources, streams, and observation framework.

## Current implementation

GPS receiver logic is supplied by `gps-nmea` as an ELF module. Firmware retains only the loader, validated kernel endpoint, and compatibility facade for `T5GpsApi`. There is no compiled-in GPS parser fallback and no TinyGPSPlus firmware dependency. The Wi-Fi provider extraction remains an earlier step in the same runtime-driver migration.

### Build independently

Use the firmware's ESP32-S3 Xtensa compiler (GCC 8.4.0, 2021r2-patch5):

```sh
python scripts/build_driver.py
```

`--cc /path/to/xtensa-esp32s3-elf-gcc` or `NATIVE_DRIVER_CC` selects an explicit compiler. The script otherwise checks PATH and PlatformIO's package directory. No firmware compilation or linked firmware image is needed.

Outputs:

- `dist/drivers/gps-nmea/driver.elf`
- `dist/drivers/gps-nmea/manifest.json`, including ELF size and SHA-256
- `dist/drivers/gps-nmea-1.0.0.t5driver.zip`, retained package-format compatibility name

The build checks ELF architecture, the single `t5_driver_get` compatibility export, and imports against the runtime's libc exports. Kernel primitives are passed through a function table; they are not global symbols exported to application ELFs. Software double-precision arithmetic is linked into the module from libgcc.

### Install and update

The device first needs firmware supporting driver ABI 1. With the device off and SD card mounted on a computer:

```sh
python scripts/install_driver.py dist/drivers/gps-nmea-1.0.0.t5driver.zip --sd-root /path/to/sd-card
```

The installer rejects unexpected ZIP members, oversized files, incompatible manifests, and mismatched hashes. It writes and flushes a staging directory before renaming it into place:

```text
/Drivers/gps-nmea/manifest.json
/Drivers/gps-nmea/driver.elf
```

On update, the old directory is retained as `/Drivers/.gps-nmea.previous`. Remove that backup after verifying the new version and before another update. For rollback, with the card offline, move the new directory aside and rename `.gps-nmea.previous` to `gps-nmea`.

For manual installation, extract the two ZIP entries into `/Drivers/gps-nmea` on the offline card. Do not place a driver under `/Apps` or flash it as firmware.

### Runtime path

1. The current GPS app requests compatibility `T5GpsApi`.
2. Firmware validates the installed manifest, required primitive APIs, architecture, exact file name, bounded size, and SHA-256.
3. Runtime exclusively claims the configured GPS UART endpoint for the owning execution context.
4. `dlopen` loads `/sd/Drivers/gps-nmea/driver.elf`; `dlsym` resolves compatibility `t5_driver_get` and negotiates ABI 1.
5. Runtime checks descriptor identity and current `position.gnss` API 1, then calls `start` with bounded kernel I/O.
6. The ELF acquires power, probes 9600/38400 baud, parses checksummed NMEA GGA/RMC, and supplies coordinates/status.
7. Stop/return clears callable capability pointers, stops the provider, unloads the ELF, and releases UART/power resources. Failures unwind ownership; failed unload retains the module handle rather than reusing a possibly live module.

Applications never retain a pointer into the driver ELF. Installed files do not start the receiver at boot. A valid NMEA checksum indicates receiver detection; a fresh valid location indicates a fix. Baud probing has a 1600 ms window, fix freshness is 5000 ms, and each read consumes at most 1024 serial bytes. Invalid navigation status invalidates the fix immediately. GP/GN and other two-character talker prefixes are accepted for GGA/RMC.

The T5S3 board binding supplies UART pins and the shared GPS/LoRa rail. This is explicitly a hardware binding, not the platform identity. EPD47 builds compile the runtime but report this endpoint unavailable. GPS and LoRa have separate rail ownership bits; releasing either leaves the rail on while the other owns it. Kernel UART calls check the claiming task.

### Build and release cycle

- Pull requests build both firmware targets, build/validate the driver ELF, run driver tests, and attach the installable ZIP to firmware CI artifacts.
- Firmware release builds include the driver package as an additional asset.
- `GPS driver package` can build an artifact without firmware.
- An explicitly published `driver-gps-nmea-v1.0.0` tag builds/tests/releases only the driver; it must match `Drivers/gps_nmea/manifest.json`.

No release or tag is created merely by this documentation. Future driver-only releases require a version change and explicit publication request.

### Verification and current limits

```sh
bash test/run_driver_test.sh
python scripts/build_driver.py
bash test/run_native_app_test.sh dist/drivers/gps-nmea/driver.elf
```

Host tests exercise failed loads/ABI negotiation/start, reload, resource cleanup, baud switching, fragmented sentences, checksums, stale/invalid fixes, timer wrap, noisy input, shared rail ownership, package integrity, installation, and rollback. On-device checks remain required for UART reception, PSRAM execution, satellite fixes, repeated entry/exit, and GPS/LoRa rail behavior.

## Migration requirements

The current `gps-nmea` + `position.gnss` binding is **not** yet the roadmap's general Device Registry, dependency graph, device-profile loader, application capability gate, hot-unplug event system, location framework, or package store. Kernel calls are synchronous; background driver tasks are outside ABI 1. SHA-256 currently detects corruption but does not authenticate a publisher.

New work should close those gaps through the parent RiscRTE abstractions rather than adding GPS-specific application APIs. In particular, location observations should become streamable/versioned data, the provider should register semantic `location.*` capabilities, and board/UART/power details should remain behind runtime-owned hardware/provider boundaries.