#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
headers="${1:-$repo/lib/hal}"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror "$repo/test/boot/headless_lifecycle_test.cpp" -o "$build/boot"
"$build/boot"
# U1 is merged; compile against the storage API at this exact PR checkout.
c++ -std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual \
  -I"$repo/test/boot/cam_stubs" -I"$headers" \
  "$repo/ports/cam/HalStorageSdmmc.cpp" "$repo/test/boot/cam_storage_test.cpp" -o "$build/storage"
"$build/storage"
"$build/storage" close
"$build/storage" stat
