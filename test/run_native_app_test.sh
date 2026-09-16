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
if [[ $# -gt 0 ]]; then
  cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
    -I"$repo_dir/lib/elf_loader/include" \
    "$repo_dir/lib/elf_loader/src/esp_elf_validate.c" "$repo_dir/test/native_apps/validate_test.c" -o "$binary"
  "$binary" "$1"
fi
python3 "$repo_dir/test/native_apps/test_symbols.py"
python3 "$repo_dir/test/native_apps/test_capability_manifest.py"
echo 'Native app launcher tests passed'
