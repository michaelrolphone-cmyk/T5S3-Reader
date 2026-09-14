#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/manifest_test.cpp" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/springboard.c" "$repo_dir/test/native_apps/springboard_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/app_store.c" "$repo_dir/test/native_apps/app_store_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/timecard.c" "$repo_dir/test/native_apps/timecard_ui_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/llm_ask.c" "$repo_dir/test/native_apps/llm_ask_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/file_browser.c" "$repo_dir/test/native_apps/file_browser_test.c" \
  "$repo_dir/test/native_apps/image_api_stub.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/gps.c" "$repo_dir/test/native_apps/gps_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/lora.c" "$repo_dir/test/native_apps/lora_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/web_server.c" "$repo_dir/test/native_apps/web_server_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/serial_monitor.c" "$repo_dir/test/native_apps/serial_monitor_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/Apps/battery.c" "$repo_dir/test/native_apps/battery_test.c" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/test_manifest.py"
echo 'Manifest, springboard, App Store, Timecard, Ask, File Browser, GPS, LoRa, Web Server, USB Serial, and Battery native app tests passed'
