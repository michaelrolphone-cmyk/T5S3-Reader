# Native applications from SD

Browse Files now lists `.elf` applications alongside existing supported files.
Open an app with touch or Confirm. The browser remains alive with its folder
and selection preserved while the app owns the display; after `app_main`
returns, the loader unloads it and the browser redraws. Loading/symbol/unload
errors are logged and shown in the browser path/status line. Firmware selection
continues to list only `.bin` files. No change to Home menu indices is required.

## Host implementation

- `lib/NativeApps/include/NativeAppLauncher.h`: `esp_err_t launch_elf_app(const char *sd_path)`.
- `NativeAppLauncher.c`: exclusive blocking session, `dlopen(sd_path, RTLD_NOW)`,
  `dlsym(handle, "app_main")`, native call, `dlclose`, explicit `dlerror` handling.
- `SdVfs.cpp`: read-only `/sd` VFS over the existing mutex-protected
  HalStorage/SdFat. It does not remount the card or create a second SD driver.
- `src/native/NativeAppHost.cpp`: main-task display/input ownership, render and
  power locks, restoration, exit-gesture draining and fresh inactivity timeout.
- `lib/elf_loader`: pinned official Espressif 1.3.3 source with documented
  compatibility changes in `UPSTREAM.md`, included by PlatformIO.

The actual reader uses Arduino on ESP-IDF 4.4.x. PlatformIO flags enable the
loader's S3 PSRAM allocation and cache-address routing in all board/release
variants. There is no separate ESP-IDF migration or user-run patch step.
The two supported reader boards are ESP32-S3 with PSRAM. File/relocated image
allocations use the loader's MALLOC_CAP_SPIRAM path; allocation failure returns
a loader error. Instruction execution uses the loader's I-bus mapping/cache
synchronization, not a cast from an arbitrary malloc buffer.

The upstream dynamic interface currently accepts but ignores RTLD mode flags;
this firmware passes RTLD_NOW and uses its eager relocation path. Absolute SD
paths and dlsym executable addresses are fixed in the vendored integration.

## Build an app

Use the same PlatformIO Xtensa S3 compiler as the firmware:

```sh
pio run -e t5s3-pro
python scripts/build_native_app.py examples/native_apps/hello.c --output dist/hello.elf
```

The script finds the compiler in PlatformIO's packages directory; use `--cc`
or NATIVE_APP_CC to select it explicitly. Copy `hello.elf` to `/apps/hello.elf`
on the SD card and select it in Browse Files. Its VFS path is
`/sd/apps/hello.elf`. The example draws text, responds to touch and exits on
Back/PWR/touch Home. CI builds and stages the sample with each firmware artifact.

Apps must be ELF32 little-endian Xtensa shared objects (ET_DYN), with a defined
GLOBAL FUNC `app_main` in `.dynsym`. The script uses the toolchain's shared-object layout and CI validates the
result against the Espressif section loader. A normal firmware flash/debug `.elf` is not an app.
This framework does not load binaries compiled for desktop CPUs or RISC-V.

## C API, ABI version 1

Include `lib/NativeApps/include/T5AppApi.h` in the app and export:

```c
__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!api || api->struct_size < sizeof(*api)) return;
    /* Draw, poll input, and return when input.exit_requested is true. */
}
```

C++ entry points must use `extern "C"`; the provided build script builds C.
The versioned table supplies screen size, clear/text/rectangle drawing,
full/partial display refresh, milliseconds, and cooperative input polling.
Callbacks run only on the launching task, which exclusively owns the renderer.
`poll` yields for 1–50 ms, updates input and feeds the watchdog. Back, PWR and
touch Home set a sticky exit request; app code must respond by returning.
Do not launch app work from the render task or call ActivityManager from an app.

The explicit host API is resolved through Espressif's symbol registry. Only
registered symbols and the enabled upstream libc exports are available;
ordinary compiled-in C++ methods are not automatically dynamically exported.
Keep C ABI structure layouts compatible. Do not retain the API pointer after
return or invoke its callbacks from worker tasks.

## Cleanup and limits

The function blocks in the firmware main task. Normal main-loop services are
paused while the app runs, and the rendering task waits on its lock. Native
apps must cooperate with the provided poll API; a nonreturning app cannot be
forcibly stopped safely by unloading its executable memory. Long computations
must poll/yield frequently. Back/PWR cannot interrupt arbitrary native code.

`dlclose` frees loader-owned allocations. Before returning, app code must free
its own allocations, close files and stop/join all tasks, timers, DMA and
callbacks that reference its image. The framework does not promise to recover
from a native fault, assertion, exit(), task deletion or watchdog reset. Apps
share firmware privileges and are trusted code, not isolated processes.

The read-only VFS is intended for loading app images; it supports four open
files and images from 52 bytes to 8 MiB. The loader holds the file buffer plus
the relocated image during loading. Actual maximum app size is lower than free
PSRAM and depends on fragmentation and other reader allocations. Structural
validation rejects incompatible/truncated headers, invalid symbol/section and
relocation ranges; it is not a proof that instructions are safe.

## Verification

`test/run_native_app_test.sh` checks launcher errors, cleanup and recursive
launch rejection. With an ELF argument it also checks the actual sample format
and corrupted/truncated variants against the pre-relocation validator. CI builds
both firmware boards and the sample app, then runs these tests.

Device acceptance remains necessary: launch/exit repeatedly, measure heap
recovery, exercise missing/invalid apps and SD errors, confirm app touch/PWR
exit, browser selection, display restoration and sleep timeout after return.
Build success alone cannot establish PSRAM instruction execution on hardware.
