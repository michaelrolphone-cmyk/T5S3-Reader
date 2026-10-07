#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/saved_network/stubs" -I"$repo_dir/src" \
  "$repo_dir/src/runtime/network/SavedNetworkConnection.cpp" \
  "$repo_dir/test/saved_network/SavedNetworkFallbackTest.cpp" -o "$binary"
"$binary"
echo 'Saved-network preferred-first fallback/retry regression passed'
