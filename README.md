# T5S3 / EPD47 Reader

[![PlatformIO Build](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml/badge.svg)](https://github.com/ShallowGreen123/t5s3-reader/actions/workflows/platformio-build.yml)

English | [中文](README_CN.md)

Firmware for the **LilyGo T5S3** and **LilyGo EPD47 ESP32-S3** 4.7-inch
e-paper devices.

This project is adapted from CrossPoint Reader. Board support is selected at
compile time so each firmware image targets exactly one hardware platform.

## Thanks

Special thanks to [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). This firmware keeps and builds on CrossPoint's activity-based UI architecture, reader logic, settings system, SD-card cache, web file transfer, and many other foundations.

This repository is not the official CrossPoint project and is not affiliated with LilyGo. It is an adaptation and experimental firmware for supported LilyGo e-paper devices.

## Target Devices

| Build target | Hardware | Status |
| --- | --- | --- |
| `t5s3-pro` | [LilyGo T5 ePaper S3](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | Existing target |
| `lilygo-epd47-s3` | [LilyGo EPD47 ESP32-S3](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3) | Compiles; physical acceptance testing is pending |

Both targets use an ESP32-S3, a 960 x 540 panel, a 540 x 960 default portrait
layout, GT911 touch, and microSD storage. The older ESP32-WROVER EPD47 is not
supported. EPD47 has no front light or detailed BQ battery telemetry, uses
GPIO21 for wake, and cannot wake from touch.

| ![](./docs/README_img/t5s3.png) | ![](./docs/README_img/t5s31.png) |
| --- | --- |

## Features

- EPUB reading with chapter parsing, layout, saved progress, and image support.
- TXT / Markdown reading.
- XTC reading.
- BMP image viewer.
- Recent books, file browser, reading cache, cover images, and sleep screen images.
- Native `.elf` applications loaded from the SD card and executed dynamically on the ESP32-S3.
- Wi-Fi file upload and web-based file management.
- Configurable fonts, font size, line spacing, margins, orientation, and refresh mode.
- Auto power-off after long inactivity when USB is not connected.
- Reader screenshots saved to the SD card under `screenshots/`.

## Requirements

- A supported LilyGo T5S3 or EPD47 ESP32-S3 device
- microSD card
- USB-C data cable
- Python 3
- PlatformIO Core, or VS Code with the PlatformIO extension

Install PlatformIO Core:

```bash
python -m pip install platformio==6.1.19
```

Clone the repository and enter the project directory:

```bash
git clone <repository-url>
cd z-T5S3-Reader
```

## Download Firmware To The Device

### Option 1: LILYGO Spark, Recommended

1. Download and open [LILYGO Spark](https://lilygo.cc/en-us/pages/lilygo-spark?srsltid=AfmBOoorTB7ptFu2LQNLRnoI2SA0zBGJTN6JpI9J3hmHEkKhBQSmeu0Y).
2. Search for your device and install the `corsspoint_lilygo_t5s3_e_paper` firmware.

This option currently applies to T5S3 only.

![LILYGO Spark firmware](./docs/README_img/lilygo_spark.png)

### Option 2: PlatformIO

1. Connect the device to your computer with USB-C.
2. Build the firmware for the connected board:

```bash
pio run -e t5s3-pro
# or
pio run -e lilygo-epd47-s3
```

3. Upload it to the device:

```bash
pio run -e t5s3-pro -t upload
# or
pio run -e lilygo-epd47-s3 -t upload
```

4. If upload mode is not detected, hold the BOOT button and press RESET, or hold BOOT while reconnecting USB, then run the upload command again.

5. Open the serial monitor if logs are needed:

```bash
pio device monitor -b 115200
```

### Option 3: Flash Download Tool

1. Download [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/production_stage/tools/flash_download_tool.html).

2. Select `esp32s3`.

![](./docs/README_img/download1.png)

3. Select a complete merged image intended for the exact board, set the flash
   address specified with that image, choose your serial port, and click
   `START`. PlatformIO's `.pio/build/<environment>/firmware.bin` and the CI
   `firmware-<board-id>.bin` artifacts are application images, not merged
   address-`0x0` recovery images.

![](./docs/README_img/download2.png)

## Firmware Update Safety

Release and CI artifacts use board-qualified names:

- `firmware-t5s3-pro.bin`
- `firmware-lilygo-epd47-s3.bin`

OTA checks select only the asset for the current board. SD-card updates also
inspect an embedded board marker and reject firmware built for the other board.
Use PlatformIO upload to recover from a failed or interrupted application
update: hold the board's BOOT button, press RESET (or reconnect USB), release
BOOT, and upload the correct environment again.

The EPD47 target links the GPL-3.0 LilyGo display driver. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before distributing an EPD47
binary.

## SD Card And Books

Put books directly in the SD card root directory, or organize them into folders.

Recommended layout:

```text
/
  Books/
    book.epub
    novel.txt
  apps/
    hello.elf
    mahjong.elf
  .sleep/
    sleep.bmp
```

The firmware creates a `.crosspoint/` directory on the SD card for settings, reading progress, cache, and cover thumbnails. If cache corruption or repeated crashes occur, back up the SD card and delete `.crosspoint/` to let the firmware regenerate it.

### How To Add Fonts

If you already have prebuilt `.cpfont` files, copy them into the SD card `.fonts/` directory, insert the SD card into the device, and then select the font in `Settings -> Reader -> Reader Font Family`.

`SourceHanSansSC` is the Chinese-capable font family currently prepared in this repository.

![](./docs/README_img/fonts1.png)

More information:

- English font generation reference: [sd-card-fonts](./docs/sd-card-fonts.md)
- Chinese font usage guide: [Chinese Font Usage Guide](./docs/Chinese%20Font%20Usage%20Guide.md)

## Native ELF Applications

The ESP32-S3 builds support small native applications stored as ELF shared objects on the SD card. These apps are separate from the main firmware: they can be copied to the card, opened from the file browser, executed, unloaded, and replaced without reflashing the reader firmware.

Native app sources in this repository live under [`Apps/`](Apps/). The current examples include a minimal hello-world app and a small Mahjong demo.

### How Native Apps Run

The native-app path is built around Espressif's ELF loader and dynamic-linking interface.

At runtime the flow is:

1. The file browser selects an `.elf` file on the SD card.
2. The firmware passes its absolute VFS path, such as `/sd/apps/mahjong.elf`, to `launch_elf_app()`.
3. The launcher exposes the SD card through a read-only `/sd` VFS used by the ELF loader.
4. The firmware registers the versioned native-app host symbol `t5_app_get_api`.
5. `dlopen(path, RTLD_NOW)` loads and relocates the application. The firmware is configured to use the ESP32-S3 ELF loader's PSRAM loading and instruction-cache mapping support.
6. `dlsym()` resolves the app's required `app_main` entry point.
7. The app runs in the owning UI task and talks to the firmware through the versioned `T5AppApi` function table.
8. When `app_main()` returns, the launcher calls `dlclose()` and releases the module.

Only one native application can run at a time. Nested or concurrent launches are rejected. ELF files are opened read-only by the loader VFS and are currently limited to 8 MiB.

### Native App ABI

Apps should include:

```c
#include "T5AppApi.h"
```

and export exactly this entry point:

```c
__attribute__((visibility("default"))) void app_main(void)
```

The current ABI is `T5_APP_ABI_VERSION == 1`. An application obtains the host API like this:

```c
const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
if (!api || api->struct_size < sizeof(*api)) return;
```

ABI v1 currently provides:

| API | Purpose |
| --- | --- |
| `screen_width()` / `screen_height()` | Query the current app drawing surface. |
| `clear()` | Clear the app surface. |
| `draw_text(x, y, text)` | Draw text. |
| `fill_rect(x, y, w, h, black)` | Draw or erase a filled rectangle. |
| `present(full_refresh)` | Present the frame using a full or partial e-paper refresh. |
| `poll(&input, wait_ms)` | Process input, yield to the firmware, and feed the watchdog. |
| `millis()` | Read the firmware millisecond counter. |

`t5_app_input_t` contains button state, touch coordinates, a tap flag, and `exit_requested`. Back, PWR, or the touch Home action makes `exit_requested` sticky so an app can return cleanly to the reader.

Apps should call `poll()` regularly; the API header recommends approximately every 20-50 ms. Native apps should not create a second independent UI loop that bypasses this call because polling also yields and feeds the watchdog.

### Minimal App

A native application can be very small:

```c
#include "T5AppApi.h"

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    if (!api || api->struct_size < sizeof(*api)) return;

    api->clear();
    api->draw_text(30, 60, "Hello from ELF");
    api->present(true);

    t5_app_input_t input;
    while (api->poll(&input, 20)) {
        if (input.exit_requested) return;
    }
}
```

See [`Apps/hello.c`](Apps/hello.c) for the smallest example and [`Apps/mahjong.c`](Apps/mahjong.c) for a larger interactive example.

### Building A Native App

Native ELF applications must be built with the ESP32-S3 Xtensa compiler, not the host computer's normal GCC. The repository includes [`scripts/build_native_app.py`](scripts/build_native_app.py), which uses the same `xtensa-esp32s3-elf-gcc` toolchain installed by PlatformIO.

First install the PlatformIO packages/toolchain if they are not already present:

```bash
pio run -e t5s3-pro
```

Then build one app:

```bash
python scripts/build_native_app.py Apps/hello.c --output dist/hello.elf
```

or:

```bash
python scripts/build_native_app.py Apps/mahjong.c --output dist/mahjong.elf
```

The build helper supplies the ELF-specific compiler/linker settings used by this project, including PIC code, ESP32-S3 long calls, hidden-by-default symbols, `-nostdlib`, `-nostartfiles`, shared-object output, and the native-app include path. It also inspects the dynamic symbol table and fails the build if a visible `app_main` function is not exported.

To validate an ELF against the native loader tests:

```bash
bash test/run_native_app_test.sh dist/hello.elf
```

### Building Every App Under `Apps/`

Each `.c` file is an independent application. A local bulk build can use:

```bash
mkdir -p dist/apps

find Apps -type f -name '*.c' -print0 | while IFS= read -r -d '' src; do
    rel="${src#Apps/}"
    name="${rel%.c}"
    name="${name//\//__}"

    python scripts/build_native_app.py \
        "$src" \
        --output "dist/apps/${name}.elf"
done
```

This maps, for example:

```text
Apps/hello.c             -> dist/apps/hello.elf
Apps/mahjong.c           -> dist/apps/mahjong.elf
Apps/games/solitaire.c   -> dist/apps/games__solitaire.elf
```

The same pattern can be used by GitHub Actions so every source under `Apps/` is compiled and the resulting `.elf` files are attached as CI or release artifacts.

### Installing And Launching An App

1. Build the `.elf`, or download a compatible `.elf` release artifact.
2. Copy it anywhere on the SD card; `/apps/` is the recommended location.
3. Insert the card and open `Browse Files`.
4. Navigate to the `.elf` file and open it.
5. Use the app normally. Back, PWR, or Home requests an exit through the native-app input API.
6. When the app returns, its ELF module is unloaded and control returns to the reader.

### Architecture And Compatibility

The native ABI is deliberately small. Apps do not link against the reader's C++ application internals. The firmware exports `t5_app_get_api()` as the single versioned native UI entry point, and the returned function table is the compatibility boundary between independently built ELF apps and the firmware.

This design provides several advantages:

- Apps can be distributed independently from firmware images.
- The firmware controls which services are intentionally exposed to native code.
- The ABI can grow by appending fields while older apps continue checking `abi_version` and `struct_size`.
- App code can execute directly as ESP32-S3 machine code instead of being interpreted.
- Executable sections can use the ELF loader's PSRAM/MMU/cache mapping path instead of consuming the main firmware image.

Native ELF applications are architecture-specific. An ELF built by this project targets the ESP32-S3 Xtensa architecture and is not a desktop executable, WebAssembly module, or generic Linux ELF.

### Security And Stability

Native ELF apps are native machine code, not a security sandbox. Only run applications from sources you trust.

The small `T5AppApi` reduces normal app coupling to firmware internals, but a malformed or malicious native binary can still crash the device, exhaust memory, trigger watchdog resets, or otherwise destabilize the running firmware. Treat `.elf` files with the same trust level as firmware extensions.

For normal applications, use only the documented `T5AppApi` surface and return from `app_main()` when `exit_requested` is set.

## Device Operation

### Basic Buttons

| Button | Function |
| --- | --- |
| BOOT | Short press: previous item / previous page |
| IO48 | Short press: next item / next page |
| BOOT | Long press: confirm / open |
| IO48 | Long press: power off |
| PWR | Turn on device power |
| RTS | Reset |
| HOME | Return to home screen |

### Power

- Long press `PWR` to turn on the device.
- Long press `IO48` to power off.
- When there is no activity for a long time and USB is not connected, the device enters power-off / low-power state automatically.
- If the device stops responding, press RESET and then long press the power button again.

### Home Screen

The home screen provides:

- Continue Reading: reopen the most recent book.
- Browse Files: browse files on the SD card.
- Recent Books: view recently opened books.
- File Transfer: upload books over Wi-Fi.
- Settings: configure the device.

Use Left/Right or Up/Down to move, Confirm to open, and Back to return.

### File Browser

- Left / Up: move up.
- Right / Down: move down.
- Confirm: open a file or folder.
- Back: go to the parent folder or return home.
- Long press Confirm: delete the selected file after confirmation.
- Opening a compatible `.elf` file launches it as a native application.

### Reading

- Right or Down: next page.
- Left or Up: previous page.
- Confirm: open the reader menu.
- Back: exit reading and return home.
- Long press Back: exit reading and return to the file browser.
- Long press page keys: chapter skip or other configured long-press behavior.
- Power + Down: take a screenshot and save it under `screenshots/` on the SD card.

### Wi-Fi Book Upload

1. Open `File Transfer` from the home screen.
2. Select and connect to Wi-Fi.
3. The device displays a web address.
4. Open the address in a browser on your computer or phone.
5. Upload EPUB, TXT, or other supported files to the SD card.
6. Press Back on the device to exit file transfer mode.

## Common Settings

In `Settings`, you can configure:

- Backlight level from 0 to 10. `0` turns it off, the default is `2`, it automatically turns off during sleep or power-off, and the saved level is restored after wake or boot.
- Font, font size, line spacing, and page margins.
- Reading orientation: portrait, landscape, inverted, and more.
- Refresh mode: quality, balanced, or fast.
- EPUB image rendering: show images, placeholders, or hide images.
- Sleep / power-off timeout.
- Sleep screen: default image, blank screen, custom BMP, or book cover.
- Button mapping.
- Wi-Fi networks.

## Notes

This firmware is still being tuned. E-paper refresh, image decoding, large TXT loading, power consumption, battery reporting, and native ELF application compatibility can vary with hardware state and firmware version. If something goes wrong, please provide serial logs, reproduction files, and exact steps when possible.

Thanks again to CrossPoint Reader and all related open-source library authors.
