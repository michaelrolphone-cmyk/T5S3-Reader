#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${READER_TYPOGRAPHY_SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
c++ "${flags[@]}" \
  -I"$repo_dir/test/native_apps/reader_stubs" -I"$repo_dir/sdk/driver" -I"$repo_dir/lib/GfxRenderer" \
  "$repo_dir/src/native/NativeReaderTypography.cpp" \
  "$repo_dir/src/native/NativeReaderTypographyBridge.cpp" \
  "$repo_dir/test/native_apps/reader_typography_test.cpp" -o "$binary"
"$binary"
