# Native ordinary USB-device MSC build

This is a source-only developer migration of `usb-device-msc-esp32s3` 0.1.8.
Generated native ELFs are experimental candidates, not replacement release
payloads. The package manifest, public capability ABI, provider/transport source,
TinyUSB adaptation and existing lifecycle behavior remain unchanged. No Runtime
exports or private import authority are added.

## Selected source and replay boundary

The selected later Wave provider CI at Waveshare commit
`9fe1c535b4de5340a1da93a382a32e3de1b0668c` pins Reader
`322fc9c0fbe6713a9fc0f3175a19550970682eaa` and upstream
[hathach/TinyUSB](https://github.com/hathach/tinyusb) 0.16.0 commit
`1eb6ce784ca9b8acbbe43dba9f1d9c26c2e80eb0`. This Reader baseline contains the
0.1.8 MSC source and T053 unaligned-host-access repair. Wave/X4 T053 assembly
requires 0.1.8 but takes the MSC output as an external input; that assembly
script alone does not identify its source commit.

A legacy rebuild of that baseline with GCC8.4.0 `esp-2021r2-patch5` and
Arduino package `3.20017.241212+sha.dcc1105b` produced 64,956 bytes, SHA-256
`5296dc67309b7f9e5915dbce1d9cfcff957a6d4d2c31846cdfae25434cbf7bd1`.
Every allocated section, its address, dynamic symbols and relocations match
selected Wave/X4 MSC artifact
`9a7cc6f87fc2a9cb5b1d65b7be1795be42cee822a2af722c8a81317a701d8461`.
Retaining historical `.comment`/`.xtensa.info` and applying the following
metadata compaction reproduces the selected complete ELF byte-for-byte:
34,892 bytes, SHA-256
`9a7cc6f87fc2a9cb5b1d65b7be1795be42cee822a2af722c8a81317a701d8461`.
This establishes an exact source replay from the selected later CI pin; it does
not identify a unique original T053 producer commit.

```sh
xtensa-esp32s3-elf-objcopy --remove-section=.xt.lit --remove-section=.xt.prop \
  --remove-section='.debug_*' --remove-section='.zdebug_*' --discard-all driver.elf
```

Loader-metadata-v4 additionally removes `.comment`/`.xtensa.info`, giving
34,656 bytes, SHA-256
`01f4a1d52483ff6dd21fbd83e711e818519cf4048441de65ac43cfc3c5908339`;
that metadata-policy difference is not a source/code mismatch. The explicit
legacy recipe remains available and its raw output remains byte-identical
after the builder migration.

## Native dependencies and build

Use the official ESP-IDF 6.1 checkout at
`fff9895c82d744c7237be8847347bdd1b07c6643` and official compiler distribution
`esp-15.2.0_20251204` (GCC15.2.0). The builder verifies these pins, refuses
modified IDF/TinyUSB tracked sources, and records compiler, input and output
hashes. No Arduino package, PlatformIO, firmware build or network download is
needed. The target compiler must be little-endian windowed ESP32-S3; the shared
compiler resolver maps a unified compiler override only to its same-installation
S3 frontend. Bare generic Xtensa compilation is rejected.

```sh
export IDF_PATH=/path/to/pinned/esp-idf
export PATH=/path/to/native/xtensa-esp-elf/bin:$PATH
export TINYUSB_SOURCE=/path/to/pinned/tinyusb
python3 scripts/build_usb_device_msc.py --tinyusb "$TINYUSB_SOURCE" \
  --validator /path/to/ordinary-elf-validator --output build/msc-native
```

`--idf` overrides `IDF_PATH`; `--cc` overrides `NATIVE_DRIVER_CC` and discovery.
The output directory must be new. Native output defaults to ignored
`build/msc-native`. `target-proof.json` records the input closure, exact imports,
function exports, target layout, optional loader validation and experimental
status. The local qualification produced 57,588 bytes, SHA-256
`e9d03ba5ed318b9920289f68eee3a740ab7e828f87be334cae41657d9efa31a4`.
Compiler frontend SHA-256 was
`f556eb8b42ff8c0bb748243afa391a216b3482b7d3724e5c2aa6870952b76b74`.

The build-only `sdk/compat/msc-idf61` overlay maps four old USB PHY LL spellings
to the equivalent pinned native USB-wrap register operations. It preserves
existing exclusive-PHY ownership, register restore order and RCC behavior. The
removed `SOC_GPIO_SUPPORT_RTC_INDEPENDENT` spelling is supplied only under the
pinned S3 profile, with static assertions for USB-only GPIO19/20 and their
IO_MUX addresses. The native GPIO LL still writes those same drive fields.
Other SoCs/SDKs are not accepted. A minimal generated sdkconfig selects only
target/header assertion options; this ordinary provider has no RTOS ABI input.

The only permitted undefined symbols remain the existing libc ceiling
`memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`. The actual native
and legacy outputs both import exactly `memcpy`, `memmove`, `memset`, `strcmp`
and export only the function `t5_driver_get`. The builder verifies ELF32,
little-endian Xtensa ET_DYN, normalized relocation targets and absence of
`s32c1i`; no GPIO/USB/RTOS import expansion is allowed.

For explicit historical inputs:

```sh
python3 scripts/build_usb_device_msc.py --tinyusb "$TINYUSB_SOURCE" \
  --framework /path/to/legacy/framework-arduinoespressif32 \
  --cc /path/to/legacy/xtensa-esp32s3-elf-gcc --output build/msc-legacy
```

## Qualification

```sh
NATIVE_MSC_INTEGRATION=1 NATIVE_MSC_ELF_VALIDATOR=/path/to/ordinary-elf-validator \
  python3 -m unittest discover -s test -p test_native_msc_sdk.py -v
TINYUSB_SOURCE="$TINYUSB_SOURCE" bash test/run_usb_device_msc_test.sh
TINYUSB_SOURCE="$TINYUSB_SOURCE" SANITIZE=1 bash test/run_usb_device_msc_test.sh
python3 test/run_usb_msc_transport_test.py \
  --soc-include "$IDF_PATH/components/soc/esp32s3/register"
SANITIZE=1 python3 test/run_usb_msc_transport_test.py \
  --soc-include "$IDF_PATH/components/soc/esp32s3/register"
```

The 12 SDK tests cover pin/compiler rejection, mutually exclusive SDK selection,
actual native ELF/import validation and 256 register-pattern adapter checks
(normal and ASan/UBSan). The existing MSC runner contains 49 lifecycle scenarios,
plus bounded-pump, delayed-packet and DWC timeout fixtures; all pass normally and
with ASan/UBSan. The production stop/wait harness checks 1,024 control/receive
combinations, idempotence and retained reset/idle/pad/wrap/clock failures with
real native register layouts (normal and UBSan). LeakSanitizer is disabled in
this executor. No device throughput, Windows eject or hardware restoration
result is claimed, and selected released product bytes are not replaced.

## CI source closure

`.github/workflows/shared-sdk61.yml` checks out the exact official IDF and
TinyUSB pins with sparse source paths, reuses its single compiler-only install,
and runs the native target/adapter contracts, all lifecycle fixtures and
native-layout stop checks. It builds this repository's ordinary host ELF
validator and attaches the experimental native output/proof. There is no
firmware/configure step, framework package install or change to historical
legacy package workflows. This workflow validates source candidates; it does
not replace selected product payloads or establish device qualification.
