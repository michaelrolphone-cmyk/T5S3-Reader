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

## Apps springboard and manifests (firmware 1.1.5)

Home → **Apps** launches `/sd/Apps/springboard.elf`. The springboard is built
from `Apps/springboard.c`; its grid, pagination, selection and touch/button
navigation execute in the ELF. The firmware supplies discovery, drawing and a
launch handoff. The springboard returns before the selected ELF loads, keeping
only one ELF resident. When that app returns, the springboard reloads and rescans
the SD card. Back leaves the springboard; Home/Power exit the Apps session.

Copy `springboard.elf` and `springboard.json` from the same release into `/Apps`
on SD. Copy every other ELF with its matching JSON sidecar there too. App Store
now downloads both assets, validates the manifest and firmware floor, stages the
pair, and rolls back failed replacements. It refuses assets without a matching
manifest. Existing standalone ELFs remain usable from Browse Files; if a sidecar
exists, its compatibility requirements are enforced there as well.

Each shipped C source has a sibling JSON file, for example `Apps/settings.json`:

```json
{
  "min_firmware_version": "1.1.5",
  "display_name": "Settings",
  "file_name": "settings.elf",
  "icon": "solid:f013"
}
```

- The minimum is a numeric `major.minor.patch` firmware API floor. Development
  and RC suffixes on the running firmware use their numeric base for this check.
  The manifest floor does not accept suffixes. This is independent of ABI v1's
  append-only struct-size check.
- Display names are UTF-8, up to 95 bytes; long labels are truncated to tile width.
- File names are plain basenames, up to 127 bytes, ending in `.elf`. They must
  match the released ELF and the sidecar basename. Paths and `..` are rejected.
- Icons use `solid:<hex-codepoint>` or `regular:<hex-codepoint>` from Font Awesome
  Classic. This avoids a firmware-maintained list of icon names and supports any
  glyph included in the installed font, including supplementary Unicode glyphs.

The launcher lists up to 128 valid installed manifests, sorted by display name,
excluding itself. Missing ELF files, malformed manifests and incomplete updates
are omitted. Incompatible apps remain visible with an update notice but cannot
launch. Installed JSON files are limited to 2 KiB.

### Shared Font Awesome drawing

Install the existing `SD_fonts/FAClassicSolid` and `SD_fonts/FAClassicRegular`
folders under `/.fonts/` or `/fonts/` on SD. For example:
`/fonts/FAClassicSolid/FAClassicSolid_18.cpfont`.

Core UI code can call `FontAwesomeIcons::draw(renderer, x, y, "solid:f013", 18)`
from `src/components/FontAwesomeIcons.h` while holding the normal render lock.
The x/y origin is the glyph cell's top-left. Supported point sizes are 12, 14,
16 and 18; other sizes round upward within that set and cap at 18. The optional
black argument supports inverted buttons. Missing fonts/glyphs draw an outlined
placeholder and return false. Icon caches are separate from the reader font;
changing the reading font does not unregister them.

Native apps use the appended `draw_icon` and `draw_label` services after checking
`struct_size`. The springboard uses the appended `installed_apps_*` functions
and `request_app_launch(index)`, then immediately returns from `app_main`.
These APIs retain the existing owner-task and render-lock requirements.

### Build and release

`python scripts/build_all_apps.py` builds every `Apps/**/*.c` and validates its
sibling JSON, output basename, icon and firmware floor. Nested names are flattened
(`Apps/games/foo.c` → `games__foo.elf`, whose JSON must name that ELF); collisions
fail the build. ELF and JSON pairs are staged together in `dist/apps/`.

CI builds and validates every shipped app on both supported targets, and uploads
the pairs with its firmware artifacts. The existing tagged release workflow runs
the same builder and publishes both `.elf` and `.json` assets. No release is
created merely by building this feature branch.

Host checks: `bash test/run_springboard_test.sh` covers firmware floors, path/icon
validation, shipped manifests, touch selection, page navigation, incompatible
apps and an empty SD app list. Hardware checks still need to cover font appearance,
e-paper refresh, SD removal, launch/return cycles and Home/Back/Power gestures.
