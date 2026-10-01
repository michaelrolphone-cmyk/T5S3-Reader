#!/usr/bin/env bash
# Pass the src directory of ArduinoJson 7.4.2 (the firmware dependency).
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
json_include="${1:?Usage: run_app_manifest_test.sh /path/to/ArduinoJson/src}"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -DCROSSPOINT_VERSION='"1.2.49"' \
  -I"$repo_dir/test/native_apps/manifest_stubs" -I"$json_include" \
  -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/src/native/AppManifest.cpp" \
  "$repo_dir/test/native_apps/runtime_manifest_test.cpp" -o "$binary"
"$binary"
echo 'Runtime app manifest tests passed'

# Exercise the same parser as the real loose-admission callback, alongside
# production HalStorage and owned-buffer admission instead of a parser mock.
c++ -std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual \
  -DU1_TEST_REAL_APP_PARSER -DCROSSPOINT_VERSION='"1.2.49"' \
  -I"$repo_dir/test/hal/storage_stubs" -I"$repo_dir/lib/hal" \
  -I"$repo_dir/test/resources/cdc_sd_stubs" -I"$repo_dir/test/native_apps/manifest_stubs" \
  -I"$json_include" -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/lib/hal/HalStorage.cpp" "$repo_dir/src/native/AppManifest.cpp" \
  "$repo_dir/src/runtime/packages/PackageExecutableAdmission.cpp" \
  "$repo_dir/src/native/ManagedAppAdmission.cpp" "$repo_dir/test/resources/managed_app_admission_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
