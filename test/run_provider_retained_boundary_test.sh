#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver")
cc "${flags[@]}" -DFIXTURE_ID='"fixture-root"' -DFIXTURE_CAPABILITY='"cap.root"' "$repo/test/drivers/provider_graph_fixture.c" -o "$build/root.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-retry"' -DFIXTURE_CAPABILITY='"cap.retry"' -DFIXTURE_QUIESCE_FAIL_ONCE -DFIXTURE_QUIESCE_FAILURES=2 "$repo/test/drivers/provider_graph_fixture.c" -o "$build/retry.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-child"' -DFIXTURE_CAPABILITY='"cap.child"' -DFIXTURE_REQUIRE='"cap.retry"' "$repo/test/drivers/provider_graph_fixture.c" -o "$build/child.so"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -I"$repo/sdk/driver" -I"$repo/test/drivers/stubs" -I"$repo/src" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/test/drivers/provider_retained_boundary_v2_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/root.so" "$build/retry.so" "$build/child.so"
