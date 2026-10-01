#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual -Itest/hal/storage_stubs -Ilib/hal -Isrc)
c++ "${flags[@]}" lib/hal/HalStorage.cpp test/hal/storage_generation_test.cpp -o "$TMP/generation"
"$TMP/generation"
c++ "${flags[@]}" lib/hal/HalStorage.cpp src/runtime/packages/InstalledCapabilityResolver.cpp test/hal/storage_snapshot_test.cpp -o "$TMP/snapshot"
"$TMP/snapshot"
python3 test/hal/storage_inventory_test.py
python3 test/hal/storage_compat_import_test.py
python3 test/hal/storage_file_lifetime_test.py
