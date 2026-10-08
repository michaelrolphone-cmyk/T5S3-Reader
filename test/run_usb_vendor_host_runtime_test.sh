#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host_repo="${1:?Pass the Reader worktree implementing USB host deadlines}"
runtime="${2:?Pass the canonical Runtime 0.1.73 source checkout}"
[[ "$(git -C "$runtime" rev-parse HEAD)" == b587df55298e0bb8e676b3d59ca13679c0267bf7 ]]
cmp "$repo/sdk/driver/RiscUsbHostDeadlinesV1.h" "$host_repo/sdk/driver/RiscUsbHostDeadlinesV1.h"
for header in driver/RiscStreamSessionProviderV1.h driver/RiscStreamResultV1.h \
  driver/RiscStreamProviderV1.h driver/RiscProviderV2.h app/RiscSerialStreamSessionV1.h; do
  cmp "$repo/sdk/$header" "$runtime/sdk/$header"
done
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
mkdir "$build/include"
cp "$host_repo/sdk/driver/RiscUsbControllerDeadlinesV1.h" "$build/include/"
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$host_repo/sdk/driver" "$host_repo/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
vendor=0
for cls in ch34x_v2 cp210x_v2 ftdi; do
  vendor=$((vendor+1))
  cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    -I"$repo/sdk/driver" "$repo/Drivers/usb_$cls/driver.c" -o "$build/class.so"
  c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -DVENDOR="$vendor" -DRISC_STREAM_HOST_TESTING -I"$repo/sdk/driver" -I"$runtime/src" \
    -I"$build/include" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" \
    "$runtime/src/runtime/streams/ProviderQueueHost.cpp" \
    "$repo/test/drivers/usb_vendor_host_runtime_test.cpp" -ldl -o "$build/test"
  for scenario in normal partial-open close-busy stale io-retained deadline; do
    "$build/test" "$build/class.so" "$scenario" "$build/host.so"
  done
done
