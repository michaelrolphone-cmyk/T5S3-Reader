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

## Repeatable Mac lab tools

`scripts/esp32_lab.py inventory` passively reports the complete macOS USB device tree and serial interfaces. It never opens a port. A USB product/port name is not a board identity. The tool requires an explicit port for `identify` (ROM query and reset); use that only after the physical device and reset scope are authorized. New relay hardware must remain passive until the owner verifies load isolation and the operation is scoped.

`capture-core` rechecks an exact MAC and captures 22 seconds of only `RTE_CORE_*` logs after reset. `flash-core` additionally requires the observed USB location, expected image SHA-256 and expected source revision. It is deliberately restricted to the verified 16 MB CAM/CH34x interface and single factory-app layout. It rejects overlapping partitions, oversized erase ranges, changed serial inventory, wrong chip/MAC and changed image bytes. It reuses the MAC-checked open session, freezes the image in a new evidence directory, writes only the app, compares full readback and the unchanged partition table, and checks running revision/MAC/core result/idle. There is a 180-second process deadline, one connection attempt and no automatic retries. Existing evidence directories are never reused. No NVS contents are read or saved.

Example on the presently verified development CAM (re-run inventory first; port/location observations may change):

```sh
python -B scripts/esp32_lab.py inventory
python -B scripts/esp32_lab.py capture-core \
  --port /dev/cu.usbserial-110 --location 1-1 \
  --mac 28:84:85:4b:a1:1c --revision 94dd82766eca2b0f9da3433a89e64f150cc0a756 \
  --lock-dir /tmp/riscrte-shared-device-locks \
  --esptool-dir "$HOME/.platformio/packages/tool-esptoolpy" \
  --out /tmp/riscrte-capture-NEW
```

`scripts/build_headless_core.py --core-dir <task-local-pio-state> --out <new-artifact-directory>` builds from a clean source revision using explicitly isolated PlatformIO state, captures build output, and freezes ELF/bin plus a hash/configuration manifest outside mutable `.pio` output. Use `--pio` for the installed PlatformIO executable when necessary. Existing task-local package links reuse installed tools; the helper does not create credentials, alter global settings or run a software update command. A build failure is recorded rather than interpreted as a device failure.

`python3 -B test/lab/test_esp32_lab.py` checks the critical wrong-chip/MAC, partition-overlap, erase-rounding and unexpected-OTA-layout guards without opening devices. Inventory and filtered capture were also exercised on the connected Mac. Initial firmware commit 94dd827 passed both normal T5S3/EPD47 builds and host parser/driver/stream suites in GitHub Actions run 36813349813; these results do not establish a later commit's status.

All device-opening commands require `--lock-dir` pointing to the SAME task-local directory for all workers. A nonblocking process-held lock covers the port and, when known, MAC before serial open through cleanup. Lock files are intentionally retained to avoid unlink/recreate races. A second pySerial `exclusive=True` advisory lock is acquired before termios/control-line setup. These locks coordinate participating tools; they cannot prevent unrelated software that ignores advisory locks. Passive inventory needs no device lock. Cross-process exclusion, same-MAC/different-port rejection and lock reuse after release are host-tested.

macOS `cu` and `tty` aliases share the same task lock. Serial close errors still release the task locks and persist a failed cleanup result. Ten host tests cover these guards. Pass a known `--mac` to `identify` to enforce identity before subsequent queries; `identify --flash-info` uses the ESP32-S3 ROM to query JEDEC flash capacity without downloading a stub or writing flash. Capacity is not proof of board revision, PSRAM, pin mapping or an acceptable partition layout. In particular, the native USB Heltec path remains identification-only; CAM flash guards are unchanged.

For active jobs on a Mac configured to sleep, prefix the command with `/usr/bin/caffeinate -i`. This assertion lasts only for that command and releases when it exits; it does not change persistent power settings. Use the same wrapper for isolated builds. Check `pmset -g assertions` during a job when verifying readiness, and retain each operation's separate evidence directory. Do not leave an unrelated indefinite keep-awake process running.

### SDMMC integration boundary

The next storage step is a bounded bootstrap adapter for the original CAM's SDMMC route (CLK39/CMD38/D0=40, one-bit mode), not a second package installer. Master `1e0188c1` does not yet contain the active U1 `PackageOrdinarySdZipAdapter` interface. Coordinate the accepted U1 revision before wiring package operations. The inspected U1 design requires archive size and arbitrary-offset reads (ZIP inspection starts near EOF), plus its existing destination staging/rename/recovery operations; a sequential-only reader is insufficient. Keep parsing, policy, authorization and transactions in the ordinary package engine.

Initial adapter acceptance must cover one controller owner, one bounded open archive, fixed-size read chunks, per-I/O and total deadlines, actual scheduler yields, short-read/removal/cancellation failures and close/revoke before handoff to an installed storage provider. Read-only mode must reject lower-layer writes and formatting, rather than relying solely on an `O_RDONLY` file handle. Arduino's default SDMMC host timeout is not sufficient evidence of bounded I/O. No SDMMC mount, formatter, installer or new flash filesystem is implemented by this lab-tool change.
