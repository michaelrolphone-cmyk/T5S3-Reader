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

# Registration must copy every string and image byte before caller buffers or
# package inspection receipts can change or be released.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/sdk/driver" -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_owned_spec_v2_test.cpp" -ldl -o "$build/ownership-test"
"$build/ownership-test" "$build/root.so"

# A by-value authenticated image digest and exact sorted import declarations
# are necessary for privileged images. Host MUST NOT claim Xtensa activation.
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_privileged_spec_v2_test.cpp" \
  -ldl -o "$build/privileged-spec-test"
"$build/privileged-spec-test"

# Failed start retains the mapped ELF and dependencies through retry.
cc "${flags[@]}" "$repo/test/drivers/provider_failed_start_fixture.c" \
  -o "$build/failed-start.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_failed_start_recovery_v2_test.cpp" \
  -ldl -o "$build/recovery-test"
"$build/recovery-test" "$build/root.so" "$build/failed-start.so"

# Failed quiescence revokes grants without unmapping active IRQ/DMA owners.
cc "${flags[@]}" -DFIXTURE_ID='"fixture-retry"' \
  -DFIXTURE_CAPABILITY='"cap.retry"' -DFIXTURE_QUIESCE_FAIL_ONCE \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/retry.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  -I"$repo/test/drivers/stubs" -I"$repo/src" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/drivers/provider_failed_teardown_v2_test.cpp" \
  -ldl -o "$build/teardown-test"
"$build/teardown-test" "$build/retry.so"
