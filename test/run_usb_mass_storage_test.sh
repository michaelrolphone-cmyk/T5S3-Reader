#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_mass_storage/driver.c" \
  -o "$build/usb-mass-storage.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_mass_storage_test.c" -ldl -o "$build/usb-mass-storage-test"
"$build/usb-mass-storage-test" "$build/usb-mass-storage.so"
exports="$(nm -D --defined-only "$build/usb-mass-storage.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected MSC ELF exports: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/usb-mass-storage.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'MSC ELF forwards to forbidden compiled firmware USB implementation' >&2
  exit 1
fi
