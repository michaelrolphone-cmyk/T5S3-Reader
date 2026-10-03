#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  ${BATTERY_TEST_SANITIZERS:--fsanitize=address,undefined -fno-omit-frame-pointer} \
  -DBOARD_XTEINK_X4_PRO \
  -I"$repo/test/native_battery/stubs" -I"$repo/src" -I"$repo/sdk/driver" \
  -I"$repo/lib/Board" -I"$repo/lib/Board_X4Pro" \
  "$repo/src/native/NativeBatteryGauge.cpp" \
  "$repo/lib/Board_X4Pro/BoardX4Pro.cpp" \
  "$repo/test/native_battery/native_battery_gauge_test.cpp" -o "$binary"
for scenario in normal failures expiry retry grantless partial release threads sleep; do "$binary" "$scenario"; done
for invalid in version size read interface generation grant; do "$binary" invalid "$invalid"; done
c++ -std=c++17 -Wall -Wextra -Werror -DBOARD_T5S3_PRO -I"$repo/src" \
  "$repo/src/native/NativeBatteryGauge.cpp" \
  "$repo/test/native_battery/native_battery_legacy_test.cpp" -o "$binary"
"$binary"
python3 "$repo/test/native_battery/boot_hook_test.py"
