#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host="${1:?Pass production USB host source d780fcbc}"
runtime="${2:?Pass canonical Runtime 0.1.73 source}"
[[ "$(git -C "$host" rev-parse HEAD)" == d780fcbc93d5472eaa0dbc3f153b2f32f0c798e3 ]]
[[ "$(git -C "$runtime" rev-parse HEAD)" == b587df55298e0bb8e676b3d59ca13679c0267bf7 ]]
cmp "$repo/sdk/driver/RiscUsbHostDeadlinesV1.h" "$host/sdk/driver/RiscUsbHostDeadlinesV1.h"
for header in driver/RiscStreamSessionProviderV1.h driver/RiscStreamResultV1.h \
  driver/RiscStreamProviderV1.h driver/RiscProviderV2.h app/RiscSerialStreamSessionV1.h; do
  cmp "$repo/sdk/$header" "$runtime/sdk/$header"
done
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir "$build/include"
cp "$repo"/sdk/driver/*.h "$build/include/"
cp "$host/sdk/driver/RiscUsbControllerDeadlinesV1.h" "$build/include/"
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-omit-frame-pointer -g);fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc.so"
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$host/sdk/driver" "$host/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -DRISC_STREAM_HOST_TESTING -I"$build/include" -I"$runtime/src" \
  -I"$runtime/sdk/app" -I"$runtime/sdk/driver" \
  "$runtime/src/runtime/streams/ProviderQueueHost.cpp" \
  "$repo/test/drivers/usb_cdc_host_retained_test.cpp" -ldl -o "$build/test"
for scenario in open-control rollback-release partial-open-release configure lines read write read-notify-busy;do
  "$build/test" "$build/host.so" "$build/cdc.so" "$scenario"
done
