#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/runtime_test.cpp" -o "$build/test"
"$build/test"
printf '#include "T5StreamApi.h"\n#include "T5SerialPortApi.h"\nint main(void) { return T5_STREAM_API_VERSION != 1 || T5_SERIAL_PORT_API_VERSION != 1; }\n' > "$build/abi.c"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" "$build/abi.c" -o "$build/abi"
"$build/abi"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/test/streams/usb_device_registry_test.cpp" -o "$build/device-registry"
"$build/device-registry"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/test/streams/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/src/native/NativeStreamBridge.cpp" \
  "$repo/src/native/NativeSerialPortBridge.cpp" "$repo/test/streams/bridge_test.cpp" -o "$build/bridge"
"$build/bridge"
