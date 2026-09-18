#!/usr/bin/env bash
# Unified Package Manager MVP HOST gate: no P-256 keys, signer provisioning,
# signed provenance or NVS cryptographic rollback policy are required.
# A successful host run is NOT on-device SD/network or power-cut acceptance.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for program in c++ openssl; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing required host tool: $program" >&2
    exit 2
  fi
done
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined
       -fno-omit-frame-pointer -pthread -I"$repo_dir/src")
for test_case in package_identity package_preflight package_json_guard \
                 package_use_gate package_transaction package_recovery \
                 package_ordinary_stage package_ordinary_installer \
                 package_driver_transition; do
  echo "== Ordinary package MVP: ${test_case} =="
  c++ "${flags[@]}" "$repo_dir/test/resources/${test_case}_test.cpp" \
      -lcrypto -o "$binary"
  "$binary"
done
echo 'PASS: ordinary package MVP host tests (four kinds, source-neutral integrity, staging, versioned driver upgrades and recoverable publication).'
echo 'Deferred signer/P-256 prototype: test/run_signed_package_experiment.sh (not an MVP gate).'
