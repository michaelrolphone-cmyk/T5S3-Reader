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
printf '#include "RiscNetworkLinkV1.h"\nint main(void) { return RISC_NETWORK_LINK_API_V1 != 1; }\n' | \
  cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" -x c - -o "$build/c-abi"
"$build/c-abi"
