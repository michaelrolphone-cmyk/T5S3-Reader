#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
exe="$build/test"
trap 'rm -rf "$build"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -DESP_PLATFORM \
  -I"$repo/test/drivers/stub_privileged_loader" \
  -I"$repo/test/drivers/stubs" \
  -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  -c -o "$build/module.o"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-overloaded-virtual \
  -I"$repo/test/hal/storage_stubs" -I"$repo/lib/hal" \
  -I"$repo/test/resources/cdc_sd_stubs" -I"$repo/test/drivers/stub_privileged_loader" \
  -I"$repo/test/drivers/stubs" -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/lib/hal/HalStorage.cpp" "$repo/src/runtime/packages/PackageExecutableAdmission.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/DeviceProviderExecutorV2.cpp" \
  "$repo/test/drivers/privileged_elf_snapshot_v1_test.cpp" "$build/module.o" \
  -lcrypto -ldl -o "$exe"
"$exe"
