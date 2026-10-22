#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$repo/test/network_link/stubs" -I"$repo/test/drivers/stubs" \
  -I"$repo/lib/NativeApps/include" -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/src/native/NativeNetworkBridge.cpp" \
  "$repo/test/network_link/network_link_custody_test.cpp" -o "$build/test"
"$build/test"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/test/network_link/network_link_provider_fixture.c" \
  -o "$build/network.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -DFIXTURE_FAIL_START '-DFIXTURE_ID="test.failed"' -I"$repo/sdk/driver" \
  "$repo/test/network_link/network_link_provider_fixture.c" -o "$build/failed.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -DFIXTURE_FAIL_START '-DFIXTURE_ID="test.dependency"' '-DFIXTURE_CAPABILITY="test.transport"' \
  -I"$repo/sdk/driver" "$repo/test/network_link/network_link_provider_fixture.c" -o "$build/dependency.so"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined \
  -I"$repo/test/drivers/stubs" -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/network_link/network_link_graph_test.cpp" -ldl -o "$build/graph-test"
"$build/graph-test" "$build/network.so" "$build/failed.so" "$build/dependency.so"
printf '#include "RiscNetworkLinkV1.h"\nint main(void) { return RISC_NETWORK_LINK_API_V1 != 1; }\n' | \
  cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" -x c - -o "$build/c-abi"
"$build/c-abi"
