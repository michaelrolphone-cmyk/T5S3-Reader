#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="${1:?Pass the clean canonical Runtime 0.1.73 source checkout}"
[[ "$(git -C "$runtime" rev-parse HEAD)" == b587df55298e0bb8e676b3d59ca13679c0267bf7 ]]
for header in driver/RiscStreamSessionProviderV1.h driver/RiscStreamResultV1.h \
  driver/RiscStreamProviderV1.h driver/RiscProviderV2.h app/RiscSerialStreamSessionV1.h; do
  cmp "$repo/sdk/$header" "$runtime/sdk/$header"
done
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then
  san=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc.so"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -DRISC_STREAM_HOST_TESTING -I"$repo/sdk/driver" -I"$runtime/src" \
  -I"$runtime/sdk/app" -I"$runtime/sdk/driver" \
  "$runtime/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/test/drivers/usb_cdc_runtime_queues_test.cpp" -ldl -o "$build/test"
for scenario in normal partial-open close-busy; do "$build/test" "$build/cdc.so" "$scenario"; done
