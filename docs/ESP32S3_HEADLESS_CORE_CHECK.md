# ESP32-S3 headless core checkpoint

This is an isolated development checkpoint, not the completed U3 runtime or U4 provisioning/camera support. It is based on master a5e2db59077cc889079668dc9cd7428b08bc32a1 and does not incorporate unmerged U1 PR #96.

## Why a separate entry point is necessary

Current `src/main.cpp:setup` calls `HalGPIO::begin`, `HalPowerManager::begin`, clock and tilt initialization before SD and display startup. `HalGPIO::begin` enters `BoardT5S3::begin`, which initializes the T5 I2C bus, touch preparation, backlight PWM, button pins, SD SPI and radio power/control pins. `HalPowerManager::begin` initializes battery management. `HalStorage` uses SPI SdFat and board pin definitions. Even SD failure invokes display initialization. These paths cannot be used on the camera board by just changing the board name.

The verified CAM reference is michaelrolphone-cmyk/ESP32-CAM_MJPEG2SD commit 67e140afb0e8612bdfffc9c9a999cc9962dc94ad. Its CAMERA_MODEL_ESP32_S3_CAM selects XCLK15, SCCB4/5, D0..D7=11,9,8,10,12,18,17,16, VSYNC6, HREF7, PCLK13, no reset/powerdown GPIO, SDMMC CLK39/CMD38/D0=40, WS2812=48. These remain source evidence and are not compiled into this checkpoint. Existing firmware on the original board independently reported OV3660, 8 MB PSRAM and a 14.6 GB SDHC card. The second board has no SD per owner; this checkpoint mounts no storage.

## What runs

`platformio.headless.ini` builds only `HeadlessCoreCheck.cpp` and the existing `StreamRuntime.cpp`, with the existing `ExecutionContext` and system `DeviceRegistry`. A bounded boot check routes a real byte payload through the existing pipe scheduler and checks completion, payload identity, context cleanup and stale-handle denial. It publishes no simulated hardware. The owner task continues pumping the same registry and yields for 10 ms every turn; progress logs are throttled to five seconds. Failure remains observable as fault idle, not a boot loop.

This entry point is guarded out of ordinary reader builds. It does not modify existing runtime primitives, board HALs, U1 code or package identities. No peripheral driver or app payload changed, so no distributable package version bump applies. It reserves UART0 only for one-way boot diagnostics, with no production UART capability. NVS initialization is bypassed for this diagnostic image so Arduino downgrade recovery cannot erase stored settings; there are no NVS consumers.

## Build and deployment

Use `pio run -c platformio.headless.ini`. The development environment pins installed Espressif32 6.5.0 / Arduino 2.0.14, QIO flash and OPI PSRAM, 16 MB flash and UART115200. This is an explicitly limited development environment; it does not change the reader environments or establish production ABI compatibility with all packages. Set `PLATFORMIO_CORE_DIR` to isolated task-local state when working on the owner's Mac. The build stamps Git revision and a dirty marker into serial output.

The generated partition table/bootloader are build artifacts, not automatically authorized flash inputs. Re-enumerate devices and recheck chip MAC in the same open session used for a write. For the verified second CAM's existing single factory partition at 0x10000 (size 0x1f0000), only the fitting application image is needed. Preserve bootloader, partition table, NVS and SD; verify application bytes by readback and compare the partition table, then check the running revision/MAC and core-check result. Never select a port by enumeration order or apply this configuration to the Heltec tracker.

## Remaining integration

There is no mounted module store, package inventory/recovery, ELF admission/loading, installed-provider dispatch, provisioning manifest or camera capture in this image. Logs explicitly say packages unavailable and provisioning unimplemented. The ordinary manager/provider scheduler under active U1 ownership must be integrated from an accepted source revision rather than duplicated here. The original dedicated CAM has the owner-authorized SD card and may be used for the later provisioning proof; the no-SD diagnostic target does not justify inventing a new flash filesystem. Its verified SDMMC route still needs an isolated bootstrap/module-store adapter and a safe handoff to any installed storage provider. Camera capture remains an installed hardware-owning ELF task; a compiled `esp_camera` bridge would not meet the runtime contract. This checkpoint is actual hardware execution of existing RiscRTE core primitives, not a claim of end-to-end RiscRTE portability or milestone completion.
