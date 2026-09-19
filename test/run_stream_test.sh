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
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/elf_endpoint_test.cpp" \
  -o "$build/elf-endpoints"
"$build/elf-endpoints"
# Both stream ABI generations use the same registry and execution context.
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
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/gnss_record_adapter_test.cpp" -o "$build/gnss-record"
"$build/gnss-record"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/location_position_subscriptions_test.cpp" -o "$build/location-subscriptions"
"$build/location-subscriptions"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/cooperative_gnss_producer_test.cpp" -o "$build/gnss-producer"
"$build/gnss-producer"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/location_lease_binding_test.cpp" -o "$build/location-lease-binding"
"$build/location-lease-binding"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/location_production_registry_test.cpp" -o "$build/location-production-registry"
"$build/location-production-registry"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/http_transfer_test.cpp" -o "$build/http-transfer"
"$build/http-transfer"
printf '#include "T5StreamApi.h"\n#include "T5SerialPortApi.h"\n#include "T5DeviceApi.h"\n#include "RiscRteLocationRecords.h"\nint main(void) { return T5_STREAM_API_VERSION != 1 || T5_SERIAL_PORT_API_VERSION != 1 || T5_DEVICE_API_VERSION != 1 || RISCRTE_LOCATION_FIX_SIZE != 52; }\n' > "$build/abi.c"
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
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/native/NativeSerialPortBridge.cpp" \
  "$repo/test/streams/usb_semantic_bridge_test.cpp" -o "$build/usb-semantic-bridge"
"$build/usb-semantic-bridge"
# Host callbacks only publish snapshots. Owner-task ticks reconcile independent
# of serial calls and deliver lifecycle events to context-owned subscribers.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/native/NativeSerialPortBridge.cpp" \
  "$repo/test/streams/usb_discovery_tick_test.cpp" -o "$build/usb-discovery-tick"
"$build/usb-discovery-tick"
# Compile the exported device observation API, independent of data-stream v2.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/native/NativeSerialPortBridge.cpp" "$repo/src/native/NativeDeviceBridge.cpp" \
  "$repo/test/streams/usb_device_api_test.cpp" -o "$build/usb-device-abi"
"$build/usb-device-abi"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/test/streams/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/src/native/NativeStreamBridge.cpp" \
  "$repo/src/native/NativeSerialPortBridge.cpp" "$repo/test/streams/bridge_test.cpp" -o "$build/bridge"
"$build/bridge"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/test/streams/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/src/native/NativeStreamBridge.cpp" \
  "$repo/src/native/NativeSerialPortBridge.cpp" \
  "$repo/test/streams/usb_direct_ownership_test.cpp" -o "$build/usb-direct-ownership"
"$build/usb-direct-ownership"
# Installed USB class ELFs shuttle bytes through published endpoints, not T5UsbApi.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" \
  "$repo/test/streams/usb_class_stream_session_test.cpp" -o "$build/usb-class-session"
"$build/usb-class-session"
