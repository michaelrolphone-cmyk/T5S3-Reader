#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g -I"$repo/sdk/driver")
if [[ -n "${SANITIZERS:-}" ]]; then
  flags+=(-fsanitize="$SANITIZERS" -fno-omit-frame-pointer)
fi
for provider in usb_hid usb_hid_mouse; do
  cc "${flags[@]}" -fPIC -fvisibility=hidden -shared \
    "$repo/Drivers/$provider/driver.c" -o "$build/$provider.so"
  exports="$(nm -D --defined-only "$build/$provider.so" | awk '{print $3}')"
  [[ "$exports" == "t5_driver_get" ]]
  if nm -D --undefined-only "$build/$provider.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
    echo "Unexpected direct USB import in $provider" >&2; exit 1
  fi
done
cc "${flags[@]}" -no-pie "$repo/test/drivers/usb_hid_mouse_test.c" -o "$build/mouse-test"
"$build/mouse-test"
cc "${flags[@]}" -no-pie "$repo/test/drivers/usb_hid_mouse_composition_test.c" -ldl -o "$build/composition-test"
"$build/composition-test" "$build/usb_hid.so" "$build/usb_hid_mouse.so"
