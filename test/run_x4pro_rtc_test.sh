#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -I"$repo/sdk/driver" "$repo/test/native_rtc/abi_layout_test.c"
san=(-fsanitize=address,undefined -fno-omit-frame-pointer)
cc -std=c11 -Wall -Wextra -Werror -pedantic "${san[@]}" \
  -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  -c "$repo/Drivers/x4pro_rtc/driver.c" -o "$build/rtc.o"
c++ -std=c++17 -Wall -Wextra -Werror -pthread "${san[@]}" -DBOARD_XTEINK_X4_PRO \
  -I"$repo/test/native_rtc/stubs" -I"$repo/test/native_battery/stubs" \
  -I"$repo/src" -I"$repo/sdk/driver" -I"$repo/lib/hal" \
  "$repo/src/native/NativeRtcClock.cpp" "$repo/lib/hal/HalClock.cpp" \
  "$repo/lib/hal/TimeZoneCatalog.cpp" "$repo/lib/hal/TimeZoneData.cpp" \
  "$repo/test/native_rtc/rtc_clock_test.cpp" "$build/rtc.o" -o "$build/rtc-test"
for scenario in cold local invalid write retained missing threads fastwake partial contracts; do
  "$build/rtc-test" "$scenario"
done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-variable "${san[@]}" -DBOARD_T5S3_PRO \
  -I"$repo/test/native_rtc/legacy_stubs" -I"$repo/test/native_rtc/stubs" -I"$repo/lib/hal" \
  "$repo/lib/hal/HalClock.cpp" "$repo/lib/hal/TimeZoneCatalog.cpp" "$repo/lib/hal/TimeZoneData.cpp" \
  "$repo/test/native_rtc/t5_legacy_clock_test.cpp" -o "$build/t5-rtc-test"
"$build/t5-rtc-test" 85063
"$build/t5-rtc-test" 8563
cat > "$build/x4pro_mmio.h" <<'HEADER'
#pragma once
#include <stdbool.h>
#include <stdint.h>
void x4pro_pin_release(uint32_t pin);
void x4pro_pin_output(uint32_t pin, bool high);
bool x4pro_pin_read(uint32_t pin);
HEADER
cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread "${san[@]}" \
  -Dt5_driver_get=x4_i2c_get -I"$build" -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  -c "$repo/Drivers/x4pro_i2c/driver.c" -o "$build/i2c.o"
cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread "${san[@]}" \
  -I"$build" -I"$repo/sdk/driver" -I"$repo/Drivers/x4pro_board" \
  "$repo/test/native_rtc/rtc_bus_integration_test.c" "$build/rtc.o" "$build/i2c.o" -o "$build/rtc-bus-test"
timeout 20s "$build/rtc-bus-test"
