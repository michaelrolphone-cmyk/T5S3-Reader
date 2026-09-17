#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cp210x_v2/driver.c" \
  -o "$build/cp210x-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_cp210x_v2_test.c" -ldl -o "$build/cp210x-v2-test"
"$build/cp210x-v2-test" "$build/cp210x-v2.so"
exports="$(nm -D --defined-only "$build/cp210x-v2.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected CP210x provider exports: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/cp210x-v2.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'CP210x ELF imports compiled-firmware USB behavior' >&2
  exit 1
fi
