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
# DEFAULT MVP: ordinary self-declared digest integrity, SAME reader for
# downloaded and SD bytes, four kinds, bounded staging, readback and failures.
# This test needs OpenSSL only for SHA-256, NEVER signing/key provisioning.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" "$repo_dir/test/resources/package_ordinary_stage_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
# One unsigned transaction engine handles all four package kinds, versioning,
# mapping leases, interrupted updates, cleanup and crash-consistent uninstall.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_ordinary_transaction_test.cpp" -o "$binary"
"$binary"
# Experimental signed-format prototype regressions below are not a requirement
# to install ordinary MVP packages; they are retained to avoid regressions in
# existing code while that experiment is isolated from default install paths.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" "$repo_dir/test/resources/package_archive_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" "$repo_dir/test/resources/package_trust_policy_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_security_floor_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/test/resources/package_floor_stubs" -I"$repo_dir/src" \
  "$repo_dir/src/runtime/packages/PackageDeviceSecurityFloor.cpp" \
  "$repo_dir/test/resources/package_device_security_floor_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -pthread -I"$repo_dir/src" \
  "$repo_dir/test/resources/package_signed_transaction_test.cpp" \
  -o "$binary"
"$binary"
python3 "$repo_dir/test/resources/package_builder_test.py"
python3 "$repo_dir/test/resources/package_stage_test.py"
python3 "$repo_dir/test/resources/package_provider_profile_test.py"
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
echo 'Native app regression tests passed, including ordinary four-kind staging/SHA-256, ordinary lifecycle/uninstall, legacy recovery and experimental signed prototype regressions'
