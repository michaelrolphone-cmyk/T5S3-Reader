#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/test/wifi_store_stubs" -I"$repo/src" \
  "$repo/src/WifiCredentialStore.cpp" "$repo/test/wifi_credential_store_fault_test.cpp" -o "$binary"
"$binary"
