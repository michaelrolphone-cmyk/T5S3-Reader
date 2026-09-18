#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/test/native_apps/manifest_test.cpp" -o "$binary"
"$binary"
# Legacy app pair recovery and mapped-ELF replacement reservations.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/resources/package_app_recovery_index_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_use_gate_test.cpp" -o "$binary"
"$binary"
# DEFAULT MVP: ordinary package integrity, common SD/online source contracts,
# four-kind stage -> verification -> publication and recoverable lifecycle.
# OpenSSL is used for SHA-256 corruption detection, NOT signing or trust roots.
for test_case in package_ordinary_stage package_ordinary_installer; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -pthread -I"$repo_dir/src" \
    "$repo_dir/test/resources/${test_case}_test.cpp" -lcrypto -o "$binary"
  "$binary"
done
for test_case in package_ordinary_transaction package_driver_transition driver_install_intake; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -pthread -I"$repo_dir/src" \
    "$repo_dir/test/resources/${test_case}_test.cpp" -o "$binary"
  "$binary"
done
python3 "$repo_dir/test/resources/package_driver_bridge_source_test.py"
# The old P-256/provenance/NVS experiment is not a normal build/merge gate.
# Run test/run_signed_package_experiment.sh explicitly only when requested.
for pair in \
  "springboard springboard_test" \
  "app_store app_store_test" \
  "settings settings_test" \
  "timecard timecard_ui_test" \
  "llm_ask llm_ask_test" \
  "gps gps_test" \
  "lora lora_test" \
  "web_server web_server_test" \
  "serial_monitor serial_monitor_test" \
  "battery battery_test" \
  "koreader_sync koreader_sync_test" \
  "koreader_auth koreader_auth_test" \
  "wifi_settings wifi_settings_test" \
  "file_transfer file_transfer_test" \
  "opds_settings opds_settings_test" \
  "clear_cache clear_cache_test" \
  "ota_update ota_update_test" \
  "sd_firmware_update sd_firmware_update_test" \
  "language_settings language_settings_test" \
  "font_manager font_manager_test" \
  "font_selection font_selection_test" \
  "status_bar_settings status_bar_settings_test" \
  "button_remap button_remap_test" \
  "time_zone time_zone_test"; do
  set -- $pair
  cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/Apps/$1.c" "$repo_dir/test/native_apps/$2.c" -o "$binary"
  "$binary"
done
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/Apps/file_browser.c" "$repo_dir/test/native_apps/file_browser_test.c" "$repo_dir/test/native_apps/image_api_stub.c" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/test_manifest.py"
python3 "$repo_dir/test/native_apps/test_app_package_install_integration.py"
echo 'Native app regressions passed: ordinary four-kind stage/publication/lifecycle, early driver intake, driver transition, legacy recovery and app UI (no mandatory package signing).'
