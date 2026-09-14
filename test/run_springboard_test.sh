#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/test/native_apps/manifest_test.cpp" -o "$binary"
"$binary"
for pair in \
  "springboard springboard_test" \
  "app_store app_store_test" \
  "timecard timecard_ui_test" \
  "llm_ask llm_ask_test" \
  "gps gps_test" \
  "lora lora_test" \
  "web_server web_server_test" \
  "serial_monitor serial_monitor_test" \
  "battery battery_test" \
  "koreader_sync koreader_sync_test" \
  "opds_settings opds_settings_test" \
  "clear_cache clear_cache_test" \
  "ota_update ota_update_test" \
  "sd_firmware_update sd_firmware_update_test" \
  "language_settings language_settings_test" \
  "font_manager font_manager_test" \
  "font_selection font_selection_test" \
  "status_bar_settings status_bar_settings_test"; do
  set -- $pair
  cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/Apps/$1.c" "$repo_dir/test/native_apps/$2.c" -o "$binary"
  "$binary"
done
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/Apps/file_browser.c" "$repo_dir/test/native_apps/file_browser_test.c" "$repo_dir/test/native_apps/image_api_stub.c" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/test_manifest.py"
echo 'Native app regression tests passed, including Manage Fonts, Font Family, and Customize Status Bar'
