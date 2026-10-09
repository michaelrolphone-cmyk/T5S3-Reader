#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${TINYUSB_SOURCE:?Pinned TinyUSB 0.16.0 checkout required}"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
python3 "$root/scripts/prepare_usb_device_stack.py" --source "$TINYUSB_SOURCE" --output "$build/stack"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -I"$root/sdk/driver" -I"$root/Drivers/usb_device_msc_esp32s3" -I"$build/stack")
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
"${CC:-cc}" "${flags[@]}" "$root/test/usb_device_msc/owner_test.c" \
 "$root/Drivers/usb_device_msc_esp32s3/driver.c" "$root/Drivers/usb_device_msc_esp32s3/StackDefaults.c" \
 "$build/stack/tusb.c" "$build/stack/common/tusb_fifo.c" "$build/stack/device/usbd.c" \
 "$build/stack/device/usbd_control.c" "$build/stack/class/msc/msc_device.c" -o "$build/test"
for scenario in malformed-eject short-eject-cdb begin-retained-zero bad-sd-token bad-phy-token lba-past-capacity invalid-cbw repeat eject-sync-retained begin-refused phy-refused phy-retained start-failed nonowner cancel-waiting suspend unconfigure disconnect read-write eject eject-no-fs prevent-eject wrong-sector-size lba-overflow write-retained stop-retained remount-retained phy-release-retained queue-overflow stale; do
 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 20s "$build/test" "$scenario"
done
