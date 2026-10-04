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
python3 "$repo/test/hal/touch_interrupt_capture_test.py"
python3 "$repo/test/hal/touch_capture_behavior_test.py"
python3 "$repo/test/hal/native_app_focus_test.py"
python3 "$repo/test/hal/activity_touch_focus_test.py"
python3 "$repo/test/hal/native_video_touch_focus_test.py"
python3 "$repo/test/hal/i2c_deadline_test.py"
python3 "$repo/test/hal/keyboard_entry_responsiveness_test.py"
"$build/input-test"

python3 "$(dirname "$0")/hal/ui_video_takeover_test.py"
