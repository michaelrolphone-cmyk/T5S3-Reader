#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" -I"$repo/lib/NativeApps/include" \
  "$repo/Drivers/gps_nmea/driver.c" -o "$build/gps.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -I"$repo/sdk/driver" \
  "$repo/test/drivers/bad_driver.c" -o "$build/bad.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/lib/NativeApps/include" \
  -I"$repo/src" -I"$repo/test/drivers/stubs" "$repo/src/runtime/drivers/GpsDriverModule.cpp" \
  "$repo/test/drivers/module_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/gps.so" "$build/bad.so"
c++ -std=c++17 -Wall -Wextra -Werror -DBOARD_T5S3_PRO \
  -I"$repo/test/drivers/stubs" -I"$repo/src" "$repo/src/runtime/resources/RadioPower.cpp" \
  "$repo/test/drivers/power_test.cpp" -o "$build/power-test"
"$build/power-test"
bash "$repo/test/run_usb_cdc_driver_test.sh"
python3 "$repo/test/drivers/package_test.py"
python3 "$repo/test/drivers/usb_package_test.py"
