#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_cdc_v2_test.c" -ldl -o "$build/cdc-v2-test"
"$build/cdc-v2-test" "$build/cdc-v2.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
  -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/drivers/provider_v2_module_test.cpp" -ldl -o "$build/provider-v2-test"
"$build/provider-v2-test" "$build/cdc-v2.so"
# An ELF with outstanding physical activity must remain mapped even after all
# generic software grants have gone away.
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" -DFIXTURE_ID='"fixture-stuck"' \
  -DFIXTURE_CAPABILITY='"cap.stuck"' -DFIXTURE_QUIESCE_FAIL \
  "$repo/test/drivers/provider_graph_fixture.c" -o "$build/stuck.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
  -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/drivers/provider_quiesce_v2_test.cpp" -ldl -o "$build/quiesce-test"
"$build/quiesce-test" "$build/stuck.so"
# Generic graph pins transitive dependencies, rejects cycles and refuses stale grants.
bash "$repo/test/run_provider_graph_v2_test.sh"
# Two independent ELFs compose without a firmware USB bridge: mock host ONLY.
bash "$repo/test/run_usb_provider_stack_v2_test.sh"
exports="$(nm -D --defined-only "$build/cdc-v2.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected ELF export: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/cdc-v2.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'USB v2 ELF imports a firmware USB implementation' >&2
  exit 1
fi
