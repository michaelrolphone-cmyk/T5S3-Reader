#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc/driver.c" -o "$build/usb-cdc.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" -I"$repo/lib/NativeApps/include" \
  "$repo/Drivers/gps_nmea/driver.c" -o "$build/gps.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_cdc_test.c" -ldl -o "$build/test"
"$build/test" "$build/usb-cdc.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
  -I"$repo/test/drivers/stubs" "$repo/src/runtime/drivers/UsbCdcDriverModule.cpp" \
  "$repo/test/drivers/usb_cdc_module_test.cpp" -ldl -o "$build/module-test"
"$build/module-test" "$build/usb-cdc.so" "$build/gps.so"
# Next-generation provider exercises actual USB class transactions through
# a separately provided usb.host capability, not firmware-owned hardware code.
bash "$repo/test/run_usb_cdc_v2_test.sh"
