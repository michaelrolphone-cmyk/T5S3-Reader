#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/test/drivers/mock_i2c_bus_elf_v2.c" \
  -o "$build/i2c.so"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fPIC \
  -fvisibility=hidden -shared -I"$repo/sdk/driver" \
  "$repo/Drivers/platform_clock_v1/driver.c" -o "$build/clock.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/board_power_t5s3_v2/driver.c" \
  -o "$build/power.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/vbus_provider_chain_v2_test.cpp" \
  -ldl -o "$build/chain-test"
"$build/chain-test" "$build/i2c.so" "$build/clock.so" "$build/power.so"
