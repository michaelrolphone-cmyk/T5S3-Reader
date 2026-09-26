#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_ftdi/driver.c" \
  -o "$build/ftdi.so"

cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_ftdi_test.c" -ldl -o "$build/ftdi-test"

"$build/ftdi-test" "$build/ftdi.so"

exports="$(nm -D --defined-only "$build/ftdi.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || {
  echo "Unexpected FTDI provider exports: $exports" >&2
  exit 1
}

if nm -D --undefined-only "$build/ftdi.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'FTDI ELF imports compiled-firmware USB implementation' >&2
  exit 1
fi
