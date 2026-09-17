#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" "$repo_dir/test/native_apps/manifest_test.cpp" -o "$binary"
"$binary"
# Recovery discovery must map legacy ELF, manifest backup and staged suffixes
# to the same safe package identity without scanning arbitrary JSON data.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/resources/package_app_recovery_index_test.cpp" -o "$binary"
"$binary"
# Loaded modules and directory updates must share a single exclusive identity
# reservation; verify pin lifecycle, mapped rollback, race and capacity cases.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_use_gate_test.cpp" -o "$binary"
"$binary"
# The common binary envelope must reject noncanonical, malformed and oversized
# metadata without loading any package entry or allocating an ELF-sized buffer.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" "$repo_dir/test/resources/package_archive_test.cpp" -o "$binary"
"$binary"
# Firmware policy must reject unknown/revoked/out-of-scope keys and ambiguous IDs
# before attempting cryptographic verification of a signed manifest.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" "$repo_dir/test/resources/package_trust_policy_test.cpp" -o "$binary"
"$binary"
# Security versions must persist per (kind, ID), never silently reset on backend
# failure, and never permit an installed floor to be lowered.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_security_floor_test.cpp" -o "$binary"
"$binary"
# Compile the actual ESP NVS-backed implementation against fault-injected host
# NVS storage; use real OpenSSL SHA-256 for the shortened collision-checked key.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/test/resources/package_floor_stubs" -I"$repo_dir/src" \
  "$repo_dir/src/runtime/packages/PackageDeviceSecurityFloor.cpp" \
  "$repo_dir/test/resources/package_device_security_floor_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
# Generate ephemeral signing keys in temp directories. Exercise the real P-256
# writer/reader and reauthenticate the sealed staged archive after SD copy races,
# short writes, seal failures and substitution by a different valid package.
python3 "$repo_dir/test/resources/package_builder_test.py"
python3 "$repo_dir/test/resources/package_stage_test.py"
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
echo 'Native app regression tests passed, including real device NVS floor faults, signed staging copy races, signer trust scope, canonical archive decoding and package recovery'
