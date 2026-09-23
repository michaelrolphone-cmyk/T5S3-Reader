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
