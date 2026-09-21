#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_ch34x_v2/driver.c" \
  -o "$build/ch34x-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_ch34x_v2_test.c" -ldl -o "$build/ch34x-v2-test"
"$build/ch34x-v2-test" "$build/ch34x-v2.so"
exports="$(nm -D --defined-only "$build/ch34x-v2.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected CH34x exports: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/ch34x-v2.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'CH34x ELF imports compiled-firmware USB implementation' >&2
  exit 1
fi
