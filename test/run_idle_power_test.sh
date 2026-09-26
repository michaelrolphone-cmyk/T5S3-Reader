#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
# Same CPU rule on both supported S3 boards, independent of USB attachment.
for board in BOARD_T5S3_PRO BOARD_LILYGO_EPD47_S3; do
  c++ -std=c++17 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3=1 -D"$board" \
    -I"$repo/test/hal/stubs" -I"$repo/lib/hal" \
    "$repo/lib/hal/HalPowerManager.cpp" "$repo/test/hal/idle_clock_test.cpp" \
    -o "$build/clock-test"
  "$build/clock-test"
done
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/bq25896/include" \
  "$repo/test/hal/usb_input_power_test.c" -o "$build/input-test"
python3 "$repo/test/hal/touch_latency_source_test.py"
"$build/input-test"
