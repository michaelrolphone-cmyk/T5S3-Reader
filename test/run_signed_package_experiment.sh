#!/usr/bin/env bash
# Deferred signed-package experiment; NOT the Unified Package Manager MVP gate.
# Run explicitly only when investigating the previously implemented P-256 path.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for program in c++ python3 openssl; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing experimental host tool: $program" >&2
    exit 2
  fi
done
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined
       -fno-omit-frame-pointer -pthread -I"$repo_dir/src")
echo '== Deferred signed transaction and NVS floor experiments =='
c++ "${flags[@]}" "$repo_dir/test/resources/package_signed_transaction_test.cpp" -o "$binary"
"$binary"
c++ "${flags[@]}" -I"$repo_dir/test/resources/package_floor_stubs" \
  "$repo_dir/src/runtime/packages/PackageDeviceSecurityFloor.cpp" \
  "$repo_dir/test/resources/package_device_security_floor_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
echo '== Deferred P-256 writer, staging, extraction, publication and reboot =='
python3 "$repo_dir/test/resources/package_builder_test.py"
python3 "$repo_dir/test/resources/package_stage_test.py"
python3 "$repo_dir/test/resources/package_provider_profile_test.py"
echo 'PASS: deferred signed-package prototype host regressions; not an MVP acceptance gate.'
