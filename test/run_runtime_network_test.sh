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
if rg -n '#include [<"]WiFi\.h|\bWiFi\.|\bWL_(CONNECTED|CONNECT_FAILED|NO_SSID_AVAIL)\b' \
    "$repo_dir/src/activities" --glob '*.[ch]' --glob '*.cpp' --glob '*.hpp' | \
    sed '/^[^:]*:[0-9]*:[[:space:]]*\/\//d' | rg .; then
  echo 'Activities must use the runtime network contracts' >&2
  exit 1
fi
