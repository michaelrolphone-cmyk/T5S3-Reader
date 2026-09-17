#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver")
cc "${flags[@]}" "$repo/test/drivers/mock_usb_controller_elf.c" -o "$build/controller.so"
cc "${flags[@]}" "$repo/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
cc "${flags[@]}" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/usb_three_elf_stack_v2_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/controller.so" "$build/host.so" "$build/cdc.so"
