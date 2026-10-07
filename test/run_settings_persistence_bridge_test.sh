#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/native_bridges/persistence_stubs" \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/src/native/NativeLanguageBridge.cpp" \
  "$repo_dir/src/native/NativeTimeZoneBridge.cpp" \
  "$repo_dir/test/native_bridges/settings_persistence_bridge_test.cpp" \
  -o "$binary"
"$binary" "$@"
