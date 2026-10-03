#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d /tmp/fs-helpers-utf8.XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT

"${CXX:-c++}" -std=c++17 -fsigned-char -fno-builtin \
  -I "$repo_root/test/fs_helpers_test_stubs" \
  "$repo_root/test/fs_helpers_utf8_sort_test.cpp" \
  -o "$build_dir/fs_helpers_utf8_sort_test"
"$build_dir/fs_helpers_utf8_sort_test"
