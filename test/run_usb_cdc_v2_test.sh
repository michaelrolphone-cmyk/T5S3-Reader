#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_cdc_v2_test.c" -ldl -o "$build/cdc-v2-test"
"$build/cdc-v2-test" "$build/cdc-v2.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
  -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/drivers/provider_v2_module_test.cpp" -ldl -o "$build/provider-v2-test"
"$build/provider-v2-test" "$build/cdc-v2.so"
# The dependent ELF exports exactly one symbol and cannot import a hard-coded
# firmware USB implementation. All USB operations go through the injected
# usb.host provider capability table, which a separate ELF must implement.
exports="$(nm -D --defined-only "$build/cdc-v2.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected ELF export: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/cdc-v2.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'USB v2 ELF imports a firmware USB implementation' >&2
  exit 1
fi
