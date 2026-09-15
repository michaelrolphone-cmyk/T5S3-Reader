# Installable GPS driver

GPS receiver logic is supplied by `gps-nmea` as an ELF module. Firmware retains
only the loader, validated kernel endpoint, and the compatibility facade for
`T5GpsApi`. There is no compiled-in GPS parser fallback and no TinyGPSPlus
firmware dependency. The Wi-Fi provider extraction remains an earlier step in
the same runtime-driver migration.

## Build independently

Use the firmware's ESP32-S3 Xtensa compiler (GCC 8.4.0, 2021r2-patch5):

```sh
python scripts/build_driver.py
```

`--cc /path/to/xtensa-esp32s3-elf-gcc` or `NATIVE_DRIVER_CC` selects an explicit
compiler. The script otherwise checks PATH and PlatformIO's package directory.
No firmware compilation or linked firmware image is needed.

Outputs:

- `dist/drivers/gps-nmea/driver.elf`
- `dist/drivers/gps-nmea/manifest.json`, including ELF size and SHA-256
- `dist/drivers/gps-nmea-1.0.0.t5driver.zip`, the installable package

The build checks ELF architecture, the single `t5_driver_get` export, and
imports against the runtime's libc exports. Kernel primitives are passed through
a function table; they are not global symbols exported to application ELFs.
Software double-precision arithmetic is linked into the module from libgcc.

## Install and update

The device first needs firmware built from this PR (driver ABI 1). Turn it off
and mount its SD card on the computer, then run:

```sh
python scripts/install_driver.py dist/drivers/gps-nmea-1.0.0.t5driver.zip --sd-root /path/to/sd-card
```

The installer rejects unexpected ZIP members, oversized files, incompatible
manifests, and mismatched hashes. It writes and flushes a staging directory before
renaming it into place. The installed layout is:

```text
/Drivers/gps-nmea/manifest.json
/Drivers/gps-nmea/driver.elf
```

On an update, the old directory is retained as `/Drivers/.gps-nmea.previous`.
After verifying the new version, remove that backup before another update.
For rollback, with the card offline, move the new `gps-nmea` directory aside and
rename `.gps-nmea.previous` to `gps-nmea`. If a power interruption leaves only
the backup, the same rename restores it. Safely eject the card after installing.

For manual installation, extract the two ZIP entries into `/Drivers/gps-nmea`
on the offline card. Do not place a driver under `/Apps` or flash it as firmware.
The GPS app can keep its existing binary/API; the updated app additionally
explains missing packages and failed starts in its footer.

## Runtime path

1. The GPS app requests the existing `T5GpsApi`.
2. The firmware validates the installed manifest, required primitive APIs,
   architecture, exact file name, bounded file size, and SHA-256.
3. The runtime exclusively claims the configured GPS UART endpoint on the
   application task.
4. `dlopen` loads `/sd/Drivers/gps-nmea/driver.elf` through the existing PSRAM ELF
   loader. `dlsym` resolves `t5_driver_get` and negotiates driver ABI 1.
5. The runtime checks the descriptor's identity and `position.gnss` API 1,
   then calls `start` with the kernel I/O table.
6. The ELF acquires power, probes 9600/38400 baud, parses checksummed NMEA GGA/RMC,
   and supplies coordinates/status through its GNSS capability.
7. Stop or app return stops the provider, clears callable capability pointers,
   unloads the ELF, and releases UART/power resources. Failed reads and starts
   also unwind resources. A failed unload retains its module handle rather than
   reusing a possibly live module.

Apps never retain a pointer into the driver ELF. Installed files do not start the
receiver at boot. A valid NMEA checksum indicates receiver detection; a fresh
valid location indicates a fix. Baud probing has a 1600 ms window, fix freshness
is 5000 ms, and each read consumes at most 1024 serial bytes. Invalid navigation
status invalidates the fix immediately. GP/GN and other two-character talker
prefixes are accepted for GGA/RMC.

The T5S3 board binding supplies the UART pins and shared GPS/LoRa rail. EPD47
builds compile the runtime but report this endpoint unavailable. GPS and LoRa
have separate rail ownership bits; releasing either leaves the rail on while
the other owns it. Kernel UART calls check the claiming task.

## Build and release cycle

- Pull requests build both firmware targets, build and validate the driver ELF,
  run driver tests, and attach the installable ZIP to the firmware CI artifacts.
- Firmware release builds include the driver package as an additional asset.
- `GPS driver package` can be run manually to build an artifact without building
  firmware.
- An explicitly published `driver-gps-nmea-v1.0.0` tag builds/tests/releases only
  the driver. The tag must match `Drivers/gps_nmea/manifest.json`. Driver releases
  do not become GitHub's latest release, so firmware/app update discovery keeps
  using the firmware release.

No release or tag is created merely by this PR. Future driver-only releases
require a version change and an explicit publication request.

## Verification and current limits

```sh
bash test/run_driver_test.sh
python scripts/build_driver.py
bash test/run_native_app_test.sh dist/drivers/gps-nmea/driver.elf
```

Host tests load the actual driver source as a host shared library through the
runtime module class. They cover failed loads/ABI negotiation/start, reload,
resource cleanup, baud switching, fragmented sentences, checksums, stale and
invalid fixes, timer wrap, and noisy input. Additional tests cover shared rail
ownership, package integrity, installation, and retained rollback packages. The
cross-compiled driver also passes the firmware ELF structural validator.

This proves the module/build/install pathway in software. On-device checks are
still required for UART reception, PSRAM execution, satellite fixes, repeated
GPS entry/exit, and GPS/LoRa rail behavior.

The first driver binding is deliberately fixed to `gps-nmea` and `position.gnss`.
It is not yet a general registry, dependency graph, device-profile loader, app
manifest capability gate, hot-unplug event system, or on-device package store.
Kernel calls are synchronous; background driver tasks are outside ABI 1. The
installer runs on a computer with an offline SD card. SHA-256 detects package
corruption; it does not authenticate a publisher. Driver modules remain trusted
native code without process isolation or package signatures.
