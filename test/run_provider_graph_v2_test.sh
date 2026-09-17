#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver")
cc "${flags[@]}" -DFIXTURE_ID='"fixture-root"' -DFIXTURE_CAPABILITY='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/root.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-root-alt"' -DFIXTURE_CAPABILITY='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/root-alt.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-child"' -DFIXTURE_CAPABILITY='"cap.child"' \
  -DFIXTURE_REQUIRE='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/child.so"
cc "${flags[@]}" -DFIXTURE_ID='"fixture-other"' -DFIXTURE_CAPABILITY='"cap.other"' \
  -DFIXTURE_REQUIRE='"cap.root"' \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/other.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_graph_v2_test.cpp" -ldl -o "$build/graph-test"
"$build/graph-test" "$build/root.so" "$build/child.so" "$build/other.so" "$build/root-alt.so"
