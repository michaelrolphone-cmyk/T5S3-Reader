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
  power locks, restoration, exit-gesture draining, fresh inactivity timeout,
  read-only directory enumeration, and the firmware-owned release catalog for
  native apps.
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

## Built-in native apps

Native application sources live under `Apps/` and are built by the existing
release workflow. No separate app build workflow is required.

- `Apps/sd_list.c`: lists the SD root, marks directories, and pages through the
  directory using the native directory API.
- `Apps/app_store.c`: connects using Wi-Fi credentials already saved by the
  firmware, lists `.elf` assets from this repository's latest GitHub release,
  and installs the selected app into the SD card's `/Apps` folder.

For an individual local build, use the same PlatformIO Xtensa S3 compiler as
the firmware:

```sh
pio run -e t5s3-pro
python scripts/build_native_app.py Apps/sd_list.c --output dist/sd_list.elf
python scripts/build_native_app.py Apps/app_store.c --output dist/app_store.elf
```

The script finds the compiler in PlatformIO's packages directory; use `--cc`
or NATIVE_APP_CC to select it explicitly. Copy an `.elf` to `/Apps/` on the SD
card and select it in Browse Files. Its VFS path is `/sd/Apps/<name>.elf`.
`sd_list.elf` lists the SD root, pages forward with touch/Confirm/Down, restarts
at the end, and exits through the normal Back/PWR/touch Home path.

`app_store.elf` refreshes the latest release on startup. Up/Down changes the
selected release asset, Confirm or touch downloads it, Right refreshes the
catalog, and Back/PWR/touch Home exits. The native ELF never receives the saved
Wi-Fi password or arbitrary HTTP/write primitives; connection, HTTPS, GitHub
release parsing, filename validation, and the SD write remain firmware-owned.
Downloads are constrained to safe `.elf` asset names under `/Apps`.

Apps must be ELF32 little-endian Xtensa shared objects (ET_DYN), with a defined
GLOBAL FUNC `app_main` in `.dynsym`. The script uses the toolchain's shared-object layout and CI validates the
result against the Espressif section loader. A normal firmware flash/debug `.elf` is not an app.
This framework does not load binaries compiled for desktop CPUs or RISC-V.

## C API, ABI version 1

Include `lib/NativeApps/include/T5AppApi.h` in the app and export:

```c
__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!api) return;
    /* Check struct_size for any extension callbacks the app uses. */
    /* Draw, poll input, and return when input.exit_requested is true. */
}
```

C++ entry points must use `extern "C"`; the provided build script builds C.
The versioned table supplies screen size, clear/text/rectangle drawing,
full/partial display refresh, milliseconds, cooperative input polling,
read-only SD directory enumeration, and the constrained release-app catalog.
Callbacks run only on the launching task, which exclusively owns the renderer.
`poll` yields for 1–50 ms, updates input and feeds the watchdog. Back, PWR and
touch Home set a sticky exit request; app code must respond by returning. Do
not launch app work from the render task or call ActivityManager from an app.

### Directory enumeration

Directory enumeration uses the same `/sd` namespace as the loader. The current
ABI permits one open directory per native app session:

```c
if (api->dir_open("/sd")) {
    t5_app_dirent_t entry;
    while (api->dir_next(&entry)) {
        /* entry.name, entry.size and entry.is_directory */
    }
    api->dir_close();
}
```

`dir_open` accepts `/sd` or paths below it, `dir_next` returns one immediate
child at a time, and `dir_close` closes the iterator. The host also closes an
outstanding directory automatically when the app returns. These callbacks are
appended to the version-1 structure; apps that depend on them should check
`struct_size` reaches `dir_close` before dereferencing the extension.

### Latest-release app catalog

The catalog extension is also append-only. A native app can request a refresh,
inspect the cached `.elf` assets, and ask the host to install one:

```c
const size_t required =
    offsetof(t5_app_api_v1, app_catalog_download) +
    sizeof(api->app_catalog_download);

if (api->struct_size >= required && api->app_catalog_refresh()) {
    uint32_t count = api->app_catalog_count();
    for (uint32_t i = 0; i < count; ++i) {
        t5_app_release_asset_t asset;
        if (api->app_catalog_get(i, &asset)) {
            /* asset.name and asset.size */
        }
    }
    /* api->app_catalog_download(selected_index); */
}
```

`app_catalog_refresh` reconnects with firmware-saved Wi-Fi when necessary,
queries `michaelrolphone-cmyk/T5S3-Reader` latest-release metadata, and caches
only safe `.elf` assets. `app_catalog_download` uses the cached asset URL and
writes only `/Apps/<asset-name>` on the SD card. Native code does not receive
the credential or release URL. Existing version-1 binaries remain compatible
because all extensions are appended after the original structure prefix and
new apps detect them with `struct_size`.

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
files and images from 52 bytes to 8 MiB. Native directory enumeration is a
separate host callback over the existing HalStorage instance and does not
relax the loader VFS's regular-file restrictions. The loader holds the file
buffer plus the relocated image during loading. Actual maximum app size is
lower than free PSRAM and depends on fragmentation and other reader allocations.
Structural validation rejects incompatible/truncated headers, invalid
symbol/section and relocation ranges; it is not a proof that instructions are
safe.

## Verification

`test/run_native_app_test.sh` checks launcher errors, cleanup and recursive
launch rejection. With an ELF argument it also checks the actual sample format
and corrupted/truncated variants against the pre-relocation validator.

Device acceptance remains necessary: launch/exit repeatedly, measure heap
recovery, exercise missing/invalid apps and SD errors, confirm app touch/PWR
exit, browser selection, display restoration and sleep timeout after return.
For `sd_list.elf`, also verify root listing, paging, directory markers, an empty
card/directory case, and return to the browser. For `app_store.elf`, verify
reconnection using a saved network, release refresh, selection/paging, install
to `/Apps`, overwrite behavior, download failure cleanup, and immediate launch
of the downloaded ELF from Browse Files. Build success alone cannot establish
PSRAM instruction execution on hardware.
