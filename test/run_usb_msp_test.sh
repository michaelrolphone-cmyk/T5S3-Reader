#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_msp/driver.c" \
  -o "$build/msp.so"

cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_msp_test.c" -ldl -o "$build/msp-test"

"$build/msp-test" "$build/msp.so"

exports="$(nm -D --defined-only "$build/msp.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || {
  echo "Unexpected MSP-FET provider exports: $exports" >&2
  exit 1
}

if nm -D --undefined-only "$build/msp.so" | grep -E 'usb_host_|nativeUsb|t5_usb_'; then
  echo 'MSP-FET ELF imports compiled-firmware USB implementation' >&2
  exit 1
fi
