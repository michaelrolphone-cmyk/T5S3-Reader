#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/runtime_network/stubs" -I"$repo_dir/src" \
  "$repo_dir/src/runtime/network/NetworkService.cpp" \
  "$repo_dir/src/providers/network/Esp32NetworkProvider.cpp" \
  "$repo_dir/test/runtime_network/network_test.cpp" -o "$binary"
"$binary"
# Keep direct radio access from creeping back into migrated activities.
# GitHub's stock runner does not provide ripgrep; using it here previously
# made the forbidden-import check silently pass without examining any files.
if ! command -v grep >/dev/null 2>&1; then
  echo 'grep is required for the network-isolation regression test' >&2
  exit 1
fi
if grep -REn --include='*.[ch]' --include='*.cpp' --include='*.hpp' \
    '#include [<"]WiFi\.h|\bWiFi\.|\bWL_(CONNECTED|CONNECT_FAILED|NO_SSID_AVAIL)\b' \
    "$repo_dir/src/activities" | \
    sed '/^[^:]*:[0-9]*:[[:space:]]*\/\//d' | grep -q .; then
  echo 'Activities must use the runtime network contracts' >&2
  exit 1
fi
