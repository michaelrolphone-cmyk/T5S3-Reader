#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fixture=(-std=c11 -Wall -Wextra -Werror -shared -fPIC -fvisibility=hidden -I"$repo/sdk/driver")
cc "${fixture[@]}" "${san[@]}" "$repo/test/drivers/provider_stream_fixture.c" -o "$build/provider.so"
cc "${fixture[@]}" "${san[@]}" -DREJECT_START "$repo/test/drivers/provider_stream_fixture.c" -o "$build/reject.so"
c++ -std=c++17 -Wall -Wextra -Werror "${san[@]}" \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" -I"$repo/sdk/driver" \
  -I"$repo/test/streams/stubs" -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" \
  "$repo/src/native/NativeStreamBridge.cpp" "$repo/src/native/NativeSerialPortBridge.cpp" \
  "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" \
  "$repo/test/streams/stubs/native_usb_checked_stop_stub.cpp" \
  "$repo/test/streams/stubs/native_usb_bridge_io_stub.cpp" \
  "$repo/test/streams/provider_context_bridge_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/provider.so" "$build/reject.so"
