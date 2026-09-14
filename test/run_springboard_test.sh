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
python3 "$repo_dir/test/native_apps/test_manifest.py"
echo 'Manifest and springboard tests passed'
