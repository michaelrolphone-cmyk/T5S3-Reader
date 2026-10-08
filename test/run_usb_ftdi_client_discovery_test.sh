#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
client="${1:?Pass the canonical paper Serial client checkout}"
runtime="${2:?Pass the canonical Runtime 0.1.73 checkout}"
[[ "$(git -C "$client" rev-parse HEAD)" == 7de606b7e07dacf95c2b986693b7e6238b9fc726 ]]
[[ "$(git -C "$runtime" rev-parse HEAD)" == b587df55298e0bb8e676b3d59ca13679c0267bf7 ]]
git -C "$client" diff --quiet 7de606b7e07dacf95c2b986693b7e6238b9fc726 -- \
  lib/PortableApps/src/PortableSerialClient.c lib/PortableApps/include/PortableSerialClient.h lib/NativeApps/include/T5SerialPortApi.h
git -C "$runtime" diff --quiet b587df55298e0bb8e676b3d59ca13679c0267bf7 -- \
  src/runtime/streams/ProviderQueueHost.cpp src/runtime/streams/ProviderQueueHost.h src/runtime/drivers/ProviderModuleV2.h
for header in RiscProviderV2.h RiscSerialPortV1.h; do cmp "$repo/sdk/driver/$header" "$client/lib/PortableApps/include/$header"; done
for header in driver/RiscStreamSessionProviderV1.h driver/RiscStreamResultV1.h \
  driver/RiscStreamProviderV1.h driver/RiscProviderV2.h app/RiscSerialStreamSessionV1.h; do cmp "$repo/sdk/$header" "$runtime/sdk/$header"; done
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
mkdir "$build/include"
cp "$client/lib/PortableApps/include/PortableSerialClient.h" "$build/include/"
san=(); if [[ "${SANITIZE:-0}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
includes=(-I"$build/include" -I"$repo/sdk/driver" -I"$repo/sdk/app" -I"$runtime/sdk/app" -I"$runtime/src" -I"$client/lib/NativeApps/include")
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_ftdi/driver.c" -o "$build/ftdi.so"
cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -DPORTABLE_SERIAL_STREAMS "${includes[@]}" \
  -c "$client/lib/PortableApps/src/PortableSerialClient.c" -o "$build/client.o"
c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -DVENDOR=3 -DPORTABLE_SERIAL_STREAMS -DRISC_STREAM_HOST_TESTING "${includes[@]}" \
  "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$repo/test/drivers/usb_ftdi_client_discovery_test.cpp" \
  "$build/client.o" -ldl -o "$build/test"
for scenario in normal zero multiple eight unknown duplicate zero-token overflow configuration-unknown partial-unknown malformed nonmatch \
  changed-during-scan reordered deadline legacy-host capacity probe stale-before-open retained configuration-retained \
  overrun legacy-streams unknown-live reconnect retained-live; do
  "$build/test" "$build/ftdi.so" "$scenario"
done

if [[ -n "${3:-}" ]]; then
  host_repo="$3"
  cmp "$repo/sdk/driver/RiscUsbHostDeadlinesV1.h" "$host_repo/sdk/driver/RiscUsbHostDeadlinesV1.h"
  cp "$host_repo/sdk/driver/RiscUsbControllerDeadlinesV1.h" "$build/include/"
  cc "${san[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    -I"$host_repo/sdk/driver" "$host_repo/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
  c++ "${san[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -DPRODUCTION_USB_HOST -DVENDOR=3 -DPORTABLE_SERIAL_STREAMS -DRISC_STREAM_HOST_TESTING "${includes[@]}" \
    "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$repo/test/drivers/usb_ftdi_client_discovery_test.cpp" \
    "$build/client.o" -ldl -o "$build/host-test"
  for scenario in normal zero multiple eight unknown configuration-unknown partial-unknown nonmatch unknown-live reconnect; do
    "$build/host-test" "$build/ftdi.so" "$scenario" "$build/host.so"
  done
fi
