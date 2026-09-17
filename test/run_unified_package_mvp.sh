#!/usr/bin/env bash
# Focused, reproducible HOST acceptance for the unified signed package MVP.
# Not a physical SD/power-cut, ELF-loader, or production-key acceptance test.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for program in c++ python3 openssl; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing required host tool: $program" >&2
    exit 2
  fi
done
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined
       -fno-omit-frame-pointer -pthread -I"$repo_dir/src")
echo '== Signed transaction recovery, rename and NVS fault simulation =='
c++ "${flags[@]}" "$repo_dir/test/resources/package_signed_transaction_test.cpp" -o "$binary"
"$binary"
echo '== Real NVS adapter, restart and fault injection =='
c++ "${flags[@]}" -I"$repo_dir/test/resources/package_floor_stubs" \
  "$repo_dir/src/runtime/packages/PackageDeviceSecurityFloor.cpp" \
  "$repo_dir/test/resources/package_device_security_floor_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
echo '== Real P-256 writer, staging, extraction, publication and reboot verification =='
python3 "$repo_dir/test/resources/package_builder_test.py"
python3 "$repo_dir/test/resources/package_stage_test.py"
echo 'PASS: host signed-package MVP (hardware acceptance and protected ELF loading remain separate).'
