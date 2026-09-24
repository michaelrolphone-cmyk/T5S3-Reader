#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_stlink/driver.c" \
  -o "$build/stlink.so"

cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_stlink_test.c" -ldl -o "$build/stlink-test"

"$build/stlink-test" "$build/stlink.so"

exports="$(nm -D --defined-only "$build/stlink.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || {
  echo "Unexpected ST-LINK provider exports: $exports" >&2
  exit 1
}

if nm -D --undefined-only "$build/stlink.so" | grep -E 'usb_host_|nativeUsb|t5_usb_'; then
  echo 'ST-LINK ELF imports compiled-firmware USB implementation' >&2
  exit 1
fi
