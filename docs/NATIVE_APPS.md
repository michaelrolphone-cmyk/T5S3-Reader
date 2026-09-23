# Native ELF applications

T5S3-Reader can load ESP32-S3 native applications from the SD card as ELF shared
objects. The framework is intended to make applications independently buildable,
installable, replaceable, and updatable without reflashing the reader firmware.

The current firmware version in `platformio.ini` is 1.1.7. The native-app ABI and
service APIs are versioned separately from the firmware version; applications
must use both the manifest firmware floor and each API table's `struct_size`
checks for compatibility.

## Architecture at a glance

A native application is an Xtensa ESP32-S3 `ET_DYN` ELF with a visible
`app_main` entry point. The firmware launches it from an absolute `/sd/...` VFS
path using Espressif's ELF loader:

1. `runNativeApp()` takes ownership of the reader UI session, renderer, input,
   and power/render locks.
2. `launch_elf_app()` registers the native host symbols and the existing SD card
   as the loader's read-only `/sd` VFS.
3. `dlopen(path, RTLD_NOW)` validates, loads, relocates, and maps the ELF through
   the ESP32-S3 PSRAM/cache/I-bus path.
4. `dlsym(handle, "app_main")` resolves the required entry point.
5. `app_main()` runs synchronously on the owning firmware task and calls only
   explicitly exported, versioned host APIs.
6. When `app_main()` returns, `dlclose()` unloads the module and releases loader
   allocations. The firmware then restores renderer state and resumes the
   calling activity or a queued firmware UI handoff.

Only one ELF may run at a time. Nested or concurrent launches are rejected.
Native apps are trusted native machine code, not isolated processes.

### Host implementation map

| Area | Source |
| --- | --- |
| Loader entry point | `lib/NativeApps/include/NativeAppLauncher.h`, `lib/NativeApps/src/NativeAppLauncher.c` |
| SD VFS used by the ELF loader | `lib/NativeApps/src/SdVfs.cpp` |
| Core UI/session host | `src/native/NativeAppHost.cpp` |
| App manifest parser/rules | `src/native/AppManifest.cpp`, `lib/NativeApps/include/AppManifestRules.h` |
| Storage and local-time services | `src/native/NativePlatformBridge.cpp` |
| Firmware keyboard/Home handoff | `src/native/NativeSystemUiBridge.cpp` |
| Settings bridge | `src/native/NativeSettingsBridge.cpp` |
| Public native headers | `lib/NativeApps/include/T5AppApi.h`, `T5StorageApi.h`, `T5SystemApi.h`, `T5SystemUiApi.h` |
| App build helpers | `scripts/build_native_app.py`, `scripts/build_all_apps.py`, `scripts/app_manifest.py` |
| Shipped apps | `Apps/` |

The reader uses Arduino on ESP-IDF 4.4.x. `platformio.ini` enables the vendored
Espressif `elf_loader` with S3 PSRAM allocation, shared-object loading, cache
address routing, I-bus mapping, and libc symbol exports. There is no separate
user patch step.

## Application package and manifest

First-class installed apps use an ELF plus a JSON sidecar with the same basename:

```text
/Apps/
  springboard.elf
  springboard.json
  app_store.elf
  app_store.json
  timecard.elf
  timecard.json
```

A shipped source has the same pairing in the repository:

```text
Apps/timecard.c
Apps/timecard.json
```

Example manifest:

```json
{
  "min_firmware_version": "1.1.5",
  "display_name": "Timecard",
  "file_name": "timecard.elf",
  "icon": "solid:f017"
}
```

Manifest rules are enforced by both the build tooling and firmware:

- `min_firmware_version` is numeric `major.minor.patch`. Each component is
  0-65535. A running development/RC firmware may have a `-` or `+` suffix; the
  manifest floor may not.
- `display_name` is non-empty UTF-8 and must fit in 95 bytes plus its terminator.
- `file_name` is a safe basename under 128 bytes, may contain letters, digits,
  `_`, `-`, and `.`, must end in `.elf`, and may not contain `..` or path
  separators. It must match the emitted release ELF basename.
- `icon` is `solid:<hex-codepoint>` or `regular:<hex-codepoint>` using a valid
  Font Awesome Classic Unicode scalar value.
- Installed manifest files are limited to 2 KiB by the firmware parser.

The manifest firmware floor and the C ABI are independent compatibility layers.
A manifest can prevent an obviously too-new app from launching, but code must
still check the specific API table members it uses. Do not assume every field in
a version-1 structure existed in the first firmware that exposed version 1.

### Legacy loose ELFs

Browse Files can still launch an ELF without a manifest. If a matching JSON
sidecar is present, the firmware enforces it before launch. The Apps springboard,
App Store, and tagged-release app workflow use ELF+JSON pairs and should be used
for normal first-class applications.

## Building apps

Native apps must be compiled for Xtensa ESP32-S3, not the host CPU. Build the
firmware once so PlatformIO installs the matching toolchain:

```sh
pio run -e t5s3-pro
```

Build one app:

```sh
python scripts/build_native_app.py Apps/hello.c --output dist/hello.elf
```

If a sibling JSON exists, the helper validates it and copies it beside the ELF.
Use `--require-manifest` when a manifest is mandatory:

```sh
python scripts/build_native_app.py Apps/hello.c \
  --output dist/apps/hello.elf \
  --require-manifest
```

The helper uses `xtensa-esp32s3-elf-gcc` with C11, PIC, S3 long calls,
hidden-by-default symbols, `-nostdlib`, `-nostartfiles`, and shared-object output.
It verifies that `.dynsym` contains a defined GLOBAL FUNC named `app_main`.

Build and validate every shipped app exactly as CI/release does:

```sh
python scripts/build_all_apps.py
```

`build_all_apps.py` builds every `Apps/**/*.c`, requires and validates its
sibling manifest, runs the native-loader test for the result, and stages the
ELF+JSON pair under `dist/apps/`. Nested source paths are flattened for release
assets:

```text
Apps/games/foo.c -> dist/apps/games__foo.elf
Apps/games/foo.json must declare "file_name": "games__foo.elf"
```

Output-name collisions fail the build.

## Entry point and API discovery

Every app must export:

```c
__attribute__((visibility("default"))) void app_main(void)
```

C++ applications must export it with `extern "C"`.

The loader explicitly registers four firmware service entry points:

| Entry point | Version | Purpose |
| --- | ---: | --- |
| `t5_app_get_api()` | `T5_APP_ABI_VERSION == 1` | UI, input, directory listing, App Store/springboard, settings, icons |
| `t5_storage_get_api()` | `T5_STORAGE_API_VERSION == 1` | SD file existence/read/atomic write/remove |
| `t5_system_get_api()` | `T5_SYSTEM_API_VERSION == 1` | Firmware-local wall clock |
| `t5_system_ui_get_api()` | `T5_SYSTEM_UI_API_VERSION == 1` | Firmware keyboard and Home navigation handoff |

Only explicitly registered host symbols plus the enabled upstream libc exports
are linkable. Firmware C++ internals are not automatically exported.

### Forward-compatible member checks

All service structures begin with a version and `struct_size`. New members are
appended. Check only as far as the last member your app needs instead of
requiring the current firmware's entire structure:

```c
#include <stddef.h>
#include "T5AppApi.h"

static bool has_draw_icon(const t5_app_api_v1 *api) {
    const size_t required =
        offsetof(t5_app_api_v1, draw_icon) + sizeof(api->draw_icon);
    return api && api->struct_size >= required && api->draw_icon;
}
```

Using `api->struct_size < sizeof(*api)` is acceptable only when an app deliberately
requires every member known at its build revision. Per-feature `offsetof` checks
provide better compatibility with older firmware.

Do not retain any API pointer, callback, resolved symbol, or app-owned task past
`app_main()` return. The ELF image can be unmapped immediately afterward.

## `T5AppApi`: UI/session API

Obtain the core API from the native application's owning task:

```c
const t5_app_api_v1 *app = t5_app_get_api(T5_APP_ABI_VERSION);
if (!app) return;
```

`t5_app_get_api()` returns `NULL` for an unsupported ABI or outside the active
native UI session.

### Core drawing and input

The stable prefix provides:

| Member | Purpose |
| --- | --- |
| `screen_width`, `screen_height` | Current drawing surface dimensions |
| `clear` | Clear the framebuffer |
| `draw_text` | Draw firmware UI text |
| `fill_rect` | Draw/erase a rectangle |
| `present` | Push the frame using full or half refresh |
| `poll` | Update input, yield, and feed the watchdog |
| `millis` | Firmware millisecond counter |

`t5_app_input_t` supplies button bits, a tap flag, touch coordinates, and a
sticky `exit_requested` flag. Apps should normally call `poll()` every 20-50 ms.
The host clamps the polling delay to 1-50 ms.

By default Back, Power, and the touch Home gesture request exit. Power and Home
remain unconditional exit/navigation gestures. Apps that need Back for internal
navigation can call:

```c
app->set_back_exits_app(false);
```

The app then handles `T5_APP_BUTTON_BACK` itself and returns when its own top
level is complete.

### Read-only directory enumeration

The core API exposes one native directory iterator per app session:

```c
if (app->dir_open("/sd/Books")) {
    t5_app_dirent_t entry;
    while (app->dir_next(&entry)) {
        /* entry.name, entry.size, entry.is_directory */
    }
    app->dir_close();
}
```

Paths use the `/sd` namespace. `dir_next()` returns immediate children only. The
host closes an outstanding iterator automatically when the ELF returns.

This directory interface is distinct from the ELF loader's read-only file VFS.
Use `T5StorageApi` below when an app needs to persist its own data.

### Latest-release App Store catalog

The firmware owns GitHub networking, saved Wi-Fi credentials, release parsing,
manifest validation, and writes into `/Apps`. A native app can operate the
constrained catalog without receiving Wi-Fi passwords or arbitrary download
URLs:

```c
if (app->app_catalog_refresh()) {
    uint32_t count = app->app_catalog_count();
    for (uint32_t i = 0; i < count; ++i) {
        t5_app_release_asset_t asset = {0};
        t5_app_manifest_t manifest = {0};
        if (app->app_catalog_get(i, &asset)) {
            /* asset.name, asset.size */
        }
        if (app->app_catalog_manifest_get &&
            app->app_catalog_manifest_get(i, &manifest)) {
            /* display_name, icon, firmware floor, compatible */
        }
    }
}
```

`app_catalog_refresh()` connects with firmware-saved Wi-Fi if necessary, queries
this repository's latest GitHub release, finds safe `.elf` assets with matching
`.json` sidecars, downloads and validates those manifests, drops invalid pairs,
and sorts the exposed catalog by manifest display name. The cache is capped at
64 apps.

`app_catalog_download(index)` re-fetches and validates the manifest, rejects an
incompatible app, downloads the ELF, verifies the release size when supplied,
and installs the pair under `/Apps`. Replacement is staged with `.part` files
and recoverable `.bak` files so a failed update does not intentionally leave a
half-updated ELF/manifest pair.

`Apps/app_store.c` is the reference implementation. It uses
`app_catalog_manifest_get()` to show display names, compatibility, and icons.

### Installed-app discovery and launch handoff

The springboard uses:

- `installed_apps_refresh()`
- `installed_apps_count()`
- `installed_apps_get()`
- `request_app_launch(index)`

The host scans `/Apps` for valid manifest/ELF pairs, rejects incomplete backup
states, excludes `springboard.elf` from its own list, caps the list at 128 apps,
and sorts it by display name. Incompatible apps remain discoverable so the UI can
show that a firmware update is required, but `request_app_launch()` rejects them.

`request_app_launch()` does not recursively load another ELF. It queues the
selected path and marks the current native session for exit. The caller must
return from `app_main()` immediately. The host unloads the current ELF first,
then starts the requested app. This one-ELF-at-a-time handoff is fundamental to
the springboard lifecycle.

### Shared Font Awesome and labels

The append-only UI helpers are:

```c
bool draw_icon(int32_t x, int32_t y, const char *icon,
               uint8_t point_size, bool black);
void draw_label(int32_t x, int32_t y, int32_t width, const char *text);
```

`draw_label()` centers and truncates using the firmware UI font. `draw_icon()`
parses the manifest icon codepoint and uses the firmware's shared Font Awesome
renderer; see the Font Awesome section below.

### Settings bridge

The current `T5AppApi` also exposes the firmware settings model without exposing
`CrossPointSettings` C++ internals:

- `settings_category_count()` / `settings_category_get()`
- `settings_count()` / `settings_get()`
- `settings_activate()`
- `settings_render()`
- `settings_touch()`

The stable category order is Display, Reader, Controls, System.
`t5_app_setting_t` contains a translated label, current value text, and one of:

- `T5_APP_SETTING_TOGGLE`
- `T5_APP_SETTING_ENUM`
- `T5_APP_SETTING_ACTION`
- `T5_APP_SETTING_VALUE`
- `T5_APP_SETTING_STRING`
- `T5_APP_SETTING_TIMEZONE`

`settings_activate()` returns `T5_APP_SETTING_UPDATED` for a simple setting that
was changed and saved, `T5_APP_SETTING_ACTION_REQUESTED` when the firmware must
open a complex activity, `T5_APP_SETTING_ERROR` on failure, or
`T5_APP_SETTING_NO_CHANGE` when applicable.

`settings_render()` and `settings_touch()` deliberately keep theme metrics,
translations, firmware widgets, and touch hit-testing inside the firmware while
the ELF owns high-level navigation. `Apps/settings.c` is the reference app.

Complex settings actions use the same safe unload/resume pattern as the system
keyboard: when `settings_activate()` reports `ACTION_REQUESTED`, the ELF returns;
the host dispatches the firmware activity after the ELF has released renderer
ownership, then relaunches the exact same ELF path when that activity finishes.

## `T5StorageApi`: SD persistence

Include `T5StorageApi.h` and request version 1:

```c
const t5_storage_api_v1 *storage =
    t5_storage_get_api(T5_STORAGE_API_VERSION);
if (!storage) return;
```

The API provides:

| Member | Behavior |
| --- | --- |
| `exists(path)` | Test an SD path |
| `read_file(path, buffer, capacity, &size)` | Read a complete file or query its size |
| `write_file_atomic(path, data, size)` | Stage a complete replacement then rename it into place |
| `remove_file(path)` | Remove a file; missing files are treated as already removed |

Paths must be `/sd` or below `/sd/`. The bridge rejects backslashes, empty path
segments, `.`/`..` segments, and trailing empty segments. The root itself cannot
be replaced or removed.

A two-pass read is supported:

```c
size_t size = 0;
if (!storage->read_file("/sd/.crosspoint/example.json", NULL, 0, &size)) return;
/* allocate size bytes */
```

On an undersized buffer, `read_file()` returns false while `size_out` still
reports the full file length.

`write_file_atomic()` creates missing parent directories through the firmware
storage layer, writes a sibling `.part`, renames the prior destination to `.bak`
when necessary, promotes the staged file, and restores the previous file if the
promotion fails. The current implementation buffers the complete payload in RAM;
it is not a streaming file API and should not be used as one.

`Apps/timecard.c` is the reference consumer of the storage API.

## `T5SystemApi`: local clock

`T5SystemApi` intentionally exposes a small system primitive rather than libc or
firmware clock internals:

```c
const t5_system_api_v1 *system_api =
    t5_system_get_api(T5_SYSTEM_API_VERSION);

t5_local_datetime_t now;
if (system_api && system_api->local_datetime(&now)) {
    /* year, month, day, hour, minute, second, weekday, yearday */
}
```

The value is the firmware's timezone-adjusted local wall clock using the current
reader timezone. Apps own their own calendar/business logic above this primitive.
The call returns false when the firmware clock cannot provide a usable time.

## `T5SystemUiApi`: firmware UI handoff

`T5SystemUiApi` is separate from the core app ABI so reusable firmware widgets can
evolve independently. It is available only during an active native UI session.

### Keyboard request and resume

A native app can request the normal firmware keyboard:

```c
const t5_system_ui_api_v1 *ui =
    t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);

if (ui && ui->keyboard_request("Name", "", 64,
                               T5_SYSTEM_KEYBOARD_TEXT, 1234)) {
    return; /* required: unload before firmware opens the keyboard */
}
```

The firmware records the exact current ELF path, unloads the ELF, opens
`KeyboardEntryActivity`, and relaunches that same ELF when the keyboard closes.
The relaunched app consumes the pending result:

```c
char text[65];
bool cancelled = false;
uint64_t cookie = 0;
if (ui->keyboard_take_result(text, sizeof(text), &cancelled, &cookie)) {
    /* result consumed; cookie is the value supplied with the request */
}
```

Keyboard input types are text, password, and URL. `max_length == 0` preserves the
firmware keyboard's unlimited-length behavior. A pending unread result prevents a
new keyboard request from silently overwriting it. A successful take consumes the
result.

### Navigate Home

`navigate_home()` queues a return to the firmware Home activity. Call it and then
return from `app_main()`; do not continue drawing after requesting a firmware
navigation handoff.

## Apps springboard

Home -> **Apps** runs `/sd/Apps/springboard.elf`; the springboard itself is not
hard-coded into the firmware UI. `Apps/springboard.c` owns its grid geometry,
pagination, selection, touch handling, and launch decisions.

The firmware owns only discovery, manifest compatibility checks, shared drawing
services, and the handoff to another ELF. The springboard returns before the
selected app loads. When the selected app later returns, the firmware reloads the
springboard and rescans `/Apps`. Power/Home leaves the Apps session; Back leaves
the springboard according to its app logic.

Copy `springboard.elf` and `springboard.json` from the same release to `/Apps`.
For the normal springboard experience, copy every other app as a matching pair as
well.

## Font Awesome Classic icons

The repository contains generated Font Awesome Classic SD fonts under:

```text
SD_fonts/FAClassicRegular/
SD_fonts/FAClassicSolid/
```

The firmware searches these family directories at these roots:

```text
/.fonts/
/fonts/
/SD_fonts/
/
```

For example, all of these layouts are supported:

```text
/fonts/FAClassicRegular/FAClassicRegular_18.cpfont
/SD_fonts/FAClassicRegular/FAClassicRegular_18.cpfont
/FAClassicRegular/FAClassicRegular_18.cpfont
```

Supported point sizes are 12, 14, 16, and 18. Requested sizes round upward within
that set and cap at 18.

The manifest syntax still accepts `solid:` and `regular:` for compatibility and
validation, but the current shared renderer intentionally tries
**FAClassicRegular first for every icon**, then falls back transparently to
FAClassicSolid when the glyph is absent in Regular. A missing font or glyph draws
an outlined placeholder and `draw_icon()` returns false.

The Font Awesome caches are independent of the selected reading font, so changing
the reader font does not unregister the icon fonts.

Core firmware code can use the same renderer through
`src/components/FontAwesomeIcons.h`.

## App Store and tagged releases

The tagged release workflow runs:

```sh
python scripts/build_all_apps.py
```

and publishes every staged `.elf` and `.json` from `dist/apps/` beside the
firmware images. The App Store reads the latest release, requires matching
manifest assets, validates compatibility before install, and installs the pair
transactionally under `/Apps`.

This pairing is part of the release contract. Do not publish a first-class app
ELF without its sidecar manifest.

## Loader and lifetime limits

The loader VFS is read-only and intended for loading ELF images. It allows four
open files and validates images from 52 bytes through 8 MiB. Actual usable app
size is lower than free PSRAM because loading can temporarily require both the
file buffer and relocated image and is affected by fragmentation and other
firmware allocations.

The native session is synchronous. Normal main-loop work is paused while the app
runs, and renderer ownership is exclusive. Long work must continue to poll/yield.
A non-returning app cannot be forcibly unloaded safely.

Before returning, an app must stop/join its own tasks, timers, DMA, interrupts,
and callbacks; close resources; and free app-owned allocations. Nothing may
continue executing code or dereferencing data from the ELF after `dlclose()`.

C++ ELF modules whose static objects require construction export both
`int app_module_init(void)` and `void app_module_fini(void)`. The loader calls
the init hook after relocation and the fini hook before hardware restoration
and `dlclose()`. The linker/build remains responsible for collecting the
constructor and destructor functions invoked by those hooks. Exporting only one
hook is rejected so partially initialized module state cannot be unloaded.
The framework does not promise recovery from native faults, assertions, `exit()`,
app-created task deletion, or watchdog resets.

The storage API and other host services are capabilities, not a sandbox. Native
code can modify allowed SD paths and runs with firmware privilege. Install only
trusted applications.

## Verification

Host tests:

```sh
bash test/run_native_app_test.sh
bash test/run_springboard_test.sh
```

`run_native_app_test.sh <elf>` also validates an actual app image and malformed /
truncated variants against the loader checks. `run_springboard_test.sh` covers
manifest rules, firmware floors, installed-app discovery, touch/page behavior,
incompatible apps, and App Store/springboard integration.

CI additionally runs `python scripts/build_all_apps.py` for both supported board
builds and publishes the staged app pairs as artifacts.

Hardware acceptance is still required. Exercise repeated launch/unload cycles,
heap recovery, invalid or incompatible manifests, SD removal, Home/Back/Power,
touch, e-paper refreshes, App Store replacement/failure recovery, Font Awesome
fallback, system keyboard resume, settings firmware-action resume, and persistent
storage across app restarts.

## Shipped reference apps

The `Apps/` directory demonstrates the framework at increasing levels:

| App | Demonstrates |
| --- | --- |
| `hello.c` | Minimal ABI acquisition, drawing, polling, exit |
| `mahjong.c` | Larger interactive native UI |
| `sd_list.c` | Native directory enumeration |
| `app_store.c` | Firmware-owned release catalog, manifests, icons, paired install |
| `springboard.c` | Installed-app discovery and one-ELF-at-a-time launch handoff |
| `settings.c` | Back navigation, settings metadata/rendering, firmware action handoff |
| `timecard.c` | Storage API, local-time API, firmware keyboard request/resume |

For new first-class applications, start with the closest shipped native example
rather than adding a built-in C++ Activity unless the feature specifically needs
direct firmware integration that is not yet represented by a native host API.
