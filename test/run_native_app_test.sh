#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/lib/NativeApps/src/NativeAppLauncher.c" \
  "$repo_dir/test/native_apps/programmer_api_stub.c" \
  "$repo_dir/test/native_apps/launcher_test.c" -o "$binary"
"$binary"
# The real firmware device ABI bridge must authorize by execution context and
# never turn manifest compatibility or observation into a permission grant.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/src/native/NativeDeviceBridge.cpp" \
  "$repo_dir/test/resources/device_bridge_v2_test.cpp" -o "$binary"
"$binary"
if [[ $# -gt 0 ]]; then
  cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
    -I"$repo_dir/lib/elf_loader/include" \
    "$repo_dir/lib/elf_loader/src/esp_elf_validate.c" "$repo_dir/test/native_apps/validate_test.c" -o "$binary"
  "$binary" "$1"
else
  cc -std=c11 -Wall -Wextra -Werror \
    "$repo_dir/lib/NativeApps/src/UnsignedDivisionCompat.c" \
    "$repo_dir/test/native_apps/unsigned_division_test.c" -o "$binary"
  "$binary"
fi
python3 "$repo_dir/test/native_apps/test_symbols.py"
python3 "$repo_dir/test/native_apps/test_capability_manifest.py"
python3 "$repo_dir/test/resources/driver_install_stack_progress_source_test.py"
echo 'Native app launcher tests passed'
