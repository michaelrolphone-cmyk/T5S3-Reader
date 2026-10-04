#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
if [[ "${BOOKMARK_TEST_SANITIZERS:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo_dir/src/util" -I"$repo_dir/lib/Utf8" \
  "$repo_dir/src/util/BookmarkUtil.cpp" "$repo_dir/lib/Utf8/Utf8.cpp" \
  "$repo_dir/test/util/bookmark_summary_test.cpp" -o "$binary"
"$binary"
