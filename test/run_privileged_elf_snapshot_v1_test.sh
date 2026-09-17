#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exe="$(mktemp)"
trap 'rm -f "$exe"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -DESP_PLATFORM \
  -I"$repo/test/drivers/stub_privileged_loader" \
  -I"$repo/test/drivers/stubs" \
  -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/drivers/privileged_elf_snapshot_v1_test.cpp" \
  -lcrypto -ldl -o "$exe"
"$exe"
