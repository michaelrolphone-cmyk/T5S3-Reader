# Shared-source SDK 6.1 build boundary

Independent ordinary application and generic driver builders can use the
ESP-IDF 6.1 compiler without loading this repository's legacy firmware build
or installing its Arduino package.

The shared-source integration baseline is Reader
`322fc9c0fbe6713a9fc0f3175a19550970682eaa`, selected by the current
Waveshare provider CI. Earlier shared-tool changes were re-applied and
retested against this successor rather than publishing the older Reader
master snapshot as a product dependency.

The verified compiler is GCC 15.2.0 `esp-15.2.0_20251204`, selected by ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`. The new `shared-sdk61.yml`
workflow installs only the official compiler archive with the SDK's SHA-256.
It does not build or modify standalone firmware.

## Compiler selection

`scripts/native_toolchain.py` honors an explicit compiler, then the relevant
`NATIVE_APP_CC` or `NATIVE_DRIVER_CC` variable, then the target frontend on
PATH, and finally an existing legacy PlatformIO tool-only package. It never
downloads a dependency or selects Arduino headers.

Use `xtensa-esp32s3-elf-gcc` from the native SDK installation. Bare
`xtensa-esp-elf-gcc` has a generic big-endian default; a unified override is
mapped only to its same-installation S3 frontend and is rejected if that
frontend is missing. This retains S3 CPU settings and the correct libgcc
multilib. Invalid overrides do not silently fall back.

Legacy GCC8 compiler paths and existing firmware/release workflows remain
supported. The shared builders do not change package capabilities or SDK ABI
headers. Development ELF output is not a release or installation.

## Frozen ordinary time ABI

Runtime's ordinary `clock_gettime` import deliberately remains the deployed
time32 ABI. Native SDK6 `time_t` is 64-bit and its timespec is 16 bytes.
The generic clock provider must therefore not compile against that native
structure even when its compiler is GCC15.

For native unified-compiler builds only, `build_platform_clock_v1.py` selects
`sdk/compat/elf-time32/time.h`. It freezes signed seconds and nanoseconds at
32 bits, an 8-byte timespec, nanoseconds at offset 4 and CLOCK_MONOTONIC=4.
The overlay rejects late inclusion after native libc time declarations and
asserts the layout. It is specific to ordinary ELF consumers; it must not be
used to compile native firmware or private IDF providers. Legacy compiler
inputs and driver source bytes are unchanged.

The same native build links the existing hidden source-owned
`UnsignedDivisionCompat.c` helper. GCC15's stock libgcc unsigned-64 division
member crashes the Xtensa shared linker; the bounded PIC helper resolves it
without adding a firmware import or exported entry point.

The clock still imports exactly `clock_gettime` and `usleep` and exports
only `t5_driver_get`. Shared app sources and other generic driver sources do
not pass libc time/stat/reentrancy structures. Hardware-owning camera source
is outside this ordinary boundary.

## Legacy standalone hardware boundaries

`scripts/build_camera_esp32s3.py` still reads the Arduino-packaged SDK at
`framework-arduinoespressif32/tools/sdk/esp32s3`. Its register-owning
LCD_CAM/GDMA provider is selected by the explicit standalone
`platformio.cam-runtime.ini` camera experiments and `ports/cam/build.py`,
whose base runtime uses Arduino. No camera-provider selection was found in
the inspected current Runtime, Waveshare, Drivers, X4 or Watch product build
scripts/configuration. That separate legacy hardware-provider port is not
part of this shared build change.

The ordinary `camera_utility` application is independent of that SDK package
and was successfully rebuilt and loader-validated with GCC15. Historical
firmware, private controller probes, SD/HTTP patches and standalone camera
builds intentionally keep their existing recipe.

On the selected source baseline, `build_display_epd_video.py` is no longer
the old ordinary video proxy. It builds the standalone T5 LCD/GDMA/M5GFX
hardware-owning provider with private ABI 3 using the T5 firmware compilation
database. This builder is retained byte-for-byte from the selected baseline;
the shared generic-tool migration does not replace it with the obsolete
proxy or claim it is an SDK6 native product provider. Current Watch, Waveshare
and X4 products select their own display providers.

The separately selected USB-device MSC provider belongs to the current
Waveshare/X4 source dependency and is qualified independently of these
unselected standalone hardware paths. Development builds do not replace the
released MSC ELF or increment/change installed package manifests.

## Verification

Local checks passed compiler-discovery tests, the aggregate generic driver
host suite, the clock host fixture with both native and frozen time headers,
the real Runtime adapter integration, three ordinary app target ELFs and
15 generic driver target ELFs. The selected successor was rebuilt and its
actual target outputs checked again after source-age conflict resolution.
GCC8 also rebuilt and loader-validated the selected successor's unchanged
legacy clock recipe, including its existing time32/link flags.

```sh
. "$IDF_PATH/export.sh"
python test/native_toolchain_test.py -v
python scripts/build_native_app.py examples/native_apps/hello.c --output dist/sdk61/hello.elf
bash test/run_native_app_test.sh dist/sdk61/hello.elf
python scripts/build_all_apps.py --id timecard
python scripts/build_all_apps.py --id camera_utility
python scripts/build_driver.py
bash test/run_native_app_test.sh dist/drivers/gps-nmea/driver.elf
python scripts/build_platform_clock_v1.py
bash test/run_native_app_test.sh dist/experimental/platform-clock-v1/driver.elf
python test/ordinary_elf_time32_test.py --runtime /path/to/native-Runtime
```

The time32 integration test uses two separately compiled translation units:
Runtime's actual time64-to-time32 adapter and the ordinary time32 consumer
with the real generic clock source. It checks sentinel canaries, millisecond
conversion, the INT32_MAX boundary, overflow, invalid nanoseconds and failure
propagation under ASan/UBSan. Local sandbox testing disables LeakSanitizer
only because ptrace prevents it from running.

Generic target builds, their exact import/export checks, and the real ELF
structural validator are distinct from hardware execution. No device,
firmware configuration, deployment, release or product default was changed.
