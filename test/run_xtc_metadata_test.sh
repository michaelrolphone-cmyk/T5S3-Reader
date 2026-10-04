#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="${XTC_SOURCE_ROOT:-$repo_dir}"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-parentheses)
if [[ "${XTC_SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" \
  -I"$repo_dir/test/xtc_metadata/stubs" -I"$source_dir/lib/Xtc/Xtc" \
  "$source_dir/lib/Xtc/Xtc/XtcParser.cpp" "$repo_dir/test/xtc_metadata/metadata_test.cpp" -o "$binary"
"$binary"
