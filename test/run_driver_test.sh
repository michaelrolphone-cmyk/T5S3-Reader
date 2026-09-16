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
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/resources/device_registry_test.cpp" -o "$build/device-registry-test"
"$build/device-registry-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/programmer/esp_rom_protocol_test.cpp" -o "$build/esp-rom-protocol-test"
"$build/esp-rom-protocol-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/lib/NativeApps/include" \
  "$repo/test/programmer/esp_rom_session_test.cpp" -o "$build/esp-rom-session-test"
"$build/esp-rom-session-test"
# Real firmware provider, stubbed serial/streams/MD5; the baseline is booted
# with a real execution context, and the second binary injects app stop/exit.
programmer_flags=(-std=c++17 -Wall -Wextra -Werror
  -I"$repo/test/programmer/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src")
c++ "${programmer_flags[@]}" "$repo/src/native/NativeEspRomBridge.cpp" \
  "$repo/test/programmer/programmer_context_fixture.cpp" \
  "$repo/test/programmer/esp_rom_provider_test.cpp" -o "$build/esp-rom-provider-test"
"$build/esp-rom-provider-test"
c++ "${programmer_flags[@]}" "$repo/src/native/NativeEspRomBridge.cpp" \
  "$repo/test/programmer/esp_rom_context_test.cpp" -o "$build/esp-rom-context-test"
"$build/esp-rom-context-test"
c++ -std=c++17 -Wall -Wextra -Werror -DBOARD_T5S3_PRO \
  -I"$repo/test/drivers/stubs" -I"$repo/src" "$repo/src/runtime/resources/RadioPower.cpp" \
  "$repo/test/drivers/power_test.cpp" -o "$build/power-test"
"$build/power-test"
bash "$repo/test/run_usb_cdc_driver_test.sh"
python3 "$repo/test/drivers/package_test.py"
python3 "$repo/test/drivers/usb_package_test.py"
