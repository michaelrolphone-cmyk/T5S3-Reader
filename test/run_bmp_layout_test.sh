#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/src/native" \
  "$repo_dir/test/native_image/bmp_layout_test.cpp" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_image/bmp_layout_source_test.py"
echo 'BMP layout overflow and bounds regressions passed'
