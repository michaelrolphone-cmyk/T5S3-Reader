#!/usr/bin/env bash
# Link production MSC/TinyUSB to the selected X4 SD fixture and real FatFs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${TINYUSB_SOURCE:?Pinned TinyUSB 0.16.0 checkout required}"
: "${RISCRTE_READER_ROOT:?Reader checkout containing storage_fatfs and X4 wire fixture required}"
: "${X4_SD_FIXTURE_ROOT:?X4 minimal/test directory containing sd_test.c required}"
: "${X4_SD_SDK_ROOT:?SDK emitted by X4 minimal/scripts/prepare_sdk.py required}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python3 "$root/scripts/prepare_usb_device_stack.py" --source "$TINYUSB_SOURCE" --output "$build/stack"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-overflow)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie);fi
sdflags=()
if [[ -n "${X4_SD_DRIVER_SOURCE:-}" ]];then sdflags+=("-DX4_SD_DRIVER_SOURCE=\"$X4_SD_DRIVER_SOURCE\"");fi
"${CC:-cc}" "${flags[@]}" "${sdflags[@]}" -I"$X4_SD_SDK_ROOT" -I"$X4_SD_FIXTURE_ROOT" \
 -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" -I"$RISCRTE_READER_ROOT/Drivers/x4pro_board" -I"$RISCRTE_READER_ROOT" \
 -c "$root/test/usb_device_msc/sd_directory_bridge.c" -o "$build/sd.o"
"${CC:-cc}" "${flags[@]}" -Wno-unused-parameter -I"$root/sdk/driver" -I"$root/Drivers/usb_device_msc_esp32s3" \
 -I"$build/stack" -I"$RISCRTE_READER_ROOT/Drivers/storage_fatfs" \
 "$root/test/usb_device_msc/directory_test.c" "$root/Drivers/usb_device_msc_esp32s3/driver.c" \
 "$root/Drivers/usb_device_msc_esp32s3/StackDefaults.c" "$build/stack/tusb.c" "$build/stack/common/tusb_fifo.c" \
 "$build/stack/device/usbd.c" "$build/stack/device/usbd_control.c" "$build/stack/class/msc/msc_device.c" \
 "$build/sd.o" "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ff.c" \
 "$RISCRTE_READER_ROOT/Drivers/storage_fatfs/fatfs/ffunicode.c" -o "$build/test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$build/test"
