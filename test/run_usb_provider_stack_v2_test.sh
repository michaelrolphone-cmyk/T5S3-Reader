#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/test/drivers/mock_usb_host_elf.c" -o "$build/host.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/usb_provider_stack_v2_test.cpp" -ldl -o "$build/stack-test"
"$build/stack-test" "$build/host.so" "$build/cdc.so"

# A genuinely new fourth class must traverse the generic provider graph and
# semantic device publisher without a firmware class dispatcher.
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/test/drivers/mock_usb_witness_controller_elf.c" \
  -o "$build/witness-controller.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_host_v2/driver.c" \
  -o "$build/production-host.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_serial_witness_v2/driver.c" \
  -o "$build/witness.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/usb_witness_provider_graph_test.cpp" -ldl \
  -o "$build/witness-graph-test"
"$build/witness-graph-test" "$build/witness-controller.so" \
  "$build/production-host.so" "$build/witness.so"
