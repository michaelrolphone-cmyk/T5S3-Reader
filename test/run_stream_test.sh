#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/runtime_test.cpp" -o "$build/test"
"$build/test"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/test/streams/record_queue_test.cpp" -o "$build/record-queue"
"$build/record-queue"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/record_registry_test.cpp" -o "$build/record-registry"
"$build/record-registry"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/http_transfer_test.cpp" -o "$build/http-transfer"
"$build/http-transfer"
printf '#include "T5StreamApi.h"\n#include "T5SerialPortApi.h"\nint main(void) { return T5_STREAM_API_VERSION != 1 || T5_SERIAL_PORT_API_VERSION != 1; }\n' > "$build/abi.c"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" "$build/abi.c" -o "$build/abi"
"$build/abi"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/execution_context_test.cpp" -o "$build/execution-context"
"$build/execution-context"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/test/streams/usb_device_registry_test.cpp" -o "$build/device-registry"
"$build/device-registry"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/test/streams/serial_provider_registry_test.cpp" -o "$build/serial-provider-registry"
"$build/serial-provider-registry"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/native/NativeSerialPortBridge.cpp" \
  "$repo/test/streams/serial_provider_bridge_test.cpp" -o "$build/serial-provider-bridge"
"$build/serial-provider-bridge"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/test/streams/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/src/native/NativeStreamBridge.cpp" \
  "$repo/src/native/NativeSerialPortBridge.cpp" "$repo/test/streams/bridge_test.cpp" -o "$build/bridge"
"$build/bridge"
