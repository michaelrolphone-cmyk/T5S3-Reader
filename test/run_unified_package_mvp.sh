#!/usr/bin/env bash
# Unified Package Manager MVP HOST gate: no P-256 keys, signer provisioning,
# signed provenance or NVS cryptographic rollback policy are required.
# A successful host run is NOT on-device SD/network or power-cut acceptance.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for program in c++ openssl python3; do
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
                 package_resource_only package_resource_transaction \
                 package_ordinary_stage package_ordinary_tree package_ordinary_manifest package_ordinary_installer \
                 package_driver_transition driver_install_intake \
                 package_rte_zip package_rte_zip_integrity package_cdc_migration \
                 package_independent_catalog package_online_catalog; do
  echo "== Ordinary package MVP: ${test_case} =="
  c++ "${flags[@]}" "$repo_dir/test/resources/${test_case}_test.cpp" \
      -lcrypto -o "$binary"
  "$binary"
done
echo '== Ordinary package MVP: production SD resource tree =='
c++ "${flags[@]}" -I"$repo_dir/test/resources/tree_stubs" \
    "$repo_dir/test/resources/package_ordinary_sd_tree_test.cpp" -o "$binary"
"$binary"
# Separate translation units detect a bridge-local gate that would appear
# correct in a one-file unit test but permit simultaneous /Drivers mutation.
echo '== Ordinary package MVP: package_mutation_gate =='
c++ "${flags[@]}" "$repo_dir/test/resources/package_mutation_gate_test.cpp" \
    "$repo_dir/test/resources/package_mutation_gate_other.cpp" \
    -o "$binary"
"$binary"
echo '== Ordinary package MVP: production CDC SD intent/recovery =='
cdc_fixture="$(mktemp -d)"
c++ "${flags[@]}" -I"$repo_dir/test/resources/cdc_sd_stubs" \
    "$repo_dir/src/runtime/packages/PackageCdcSdMigration.cpp" \
    "$repo_dir/test/resources/package_cdc_sd_migration_test.cpp" -lcrypto -o "$binary"
"$binary" "$cdc_fixture"
rmdir "$cdc_fixture" 2>/dev/null || true
python3 "$repo_dir/test/resources/package_signing_absence_test.py"
python3 "$repo_dir/test/resources/package_catalog_roundtrip_test.py"
python3 "$repo_dir/test/resources/package_nested_zip_test.py"
python3 "$repo_dir/test/resources/resource_only_delivery_test.py"
python3 "$repo_dir/test/resources/package_sd_zip_stage_test.py"
python3 "$repo_dir/test/resources/release_runtime_identity_test.py"
python3 "$repo_dir/test/resources/package_driver_bridge_source_test.py"
echo 'PASS: ordinary package MVP host tests (four kinds, shared mutation gate, runtime-compatible release identity, source-neutral integrity, early download refusal, ZIP CRC/topology, staging, versioned driver upgrades and recoverable publication).'
