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
"$build/storage" metadata
"$build/storage" close
"$build/storage" stat
# FatFs can be configured with only its 13-byte short-name field. Bound the
# source as well as the public copied destination before any string operation.
c++ -std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual -DFAKE_CAM_NAME_CAPACITY=13 \
  -I"$repo/test/boot/cam_stubs" -I"$headers" \
  "$repo/ports/cam/HalStorageSdmmc.cpp" "$repo/test/boot/cam_storage_test.cpp" -o "$build/storage-short-name"
"$build/storage-short-name" metadata
