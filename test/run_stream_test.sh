#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

cxx=(c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
inc=(-I"$repo/lib/NativeApps/include" -I"$repo/src")
stub_inc=(-I"$repo/test/streams/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src")

# Compile production translation units once. The previous runner rebuilt
# StreamRuntime.cpp for nearly every test and rebuilt the native bridge sources
# multiple times under identical flags, turning a small host suite into minutes
# of redundant compiler work.
"${cxx[@]}" "${inc[@]}" -c "$repo/src/runtime/streams/StreamRuntime.cpp" -o "$build/stream-runtime.o"
"${cxx[@]}" "${inc[@]}" -c "$repo/src/native/NativeSerialPortBridge.cpp" -o "$build/serial-bridge.o"
"${cxx[@]}" "${stub_inc[@]}" -c "$repo/src/native/NativeSerialPortBridge.cpp" -o "$build/serial-bridge-stub.o"
"${cxx[@]}" "${stub_inc[@]}" -c "$repo/src/native/NativeStreamBridge.cpp" -o "$build/stream-bridge-stub.o"

run_with_runtime() {
  local name="$1"
  local source="$2"
  "${cxx[@]}" "${inc[@]}" "$source" "$build/stream-runtime.o" -o "$build/$name"
  "$build/$name"
}

run_plain() {
  local name="$1"
  local source="$2"
  "${cxx[@]}" "${inc[@]}" "$source" -o "$build/$name"
  "$build/$name"
}

run_with_runtime test "$repo/test/streams/runtime_test.cpp"
run_plain record-queue "$repo/test/streams/record_queue_test.cpp"
run_with_runtime record-registry "$repo/test/streams/record_registry_test.cpp"
run_with_runtime gnss-record "$repo/test/streams/gnss_record_adapter_test.cpp"
run_with_runtime location-subscriptions "$repo/test/streams/location_position_subscriptions_test.cpp"
run_with_runtime gnss-producer "$repo/test/streams/cooperative_gnss_producer_test.cpp"
run_with_runtime location-lease-binding "$repo/test/streams/location_lease_binding_test.cpp"
run_with_runtime location-production-registry "$repo/test/streams/location_production_registry_test.cpp"
run_with_runtime http-transfer "$repo/test/streams/http_transfer_test.cpp"

printf '#include "T5StreamApi.h"\n#include "T5SerialPortApi.h"\n#include "T5DeviceApi.h"\n#include "RiscRteLocationRecords.h"\nint main(void) { return T5_STREAM_API_VERSION != 1 || T5_SERIAL_PORT_API_VERSION != 1 || T5_DEVICE_API_VERSION != 1 || RISCRTE_LOCATION_FIX_SIZE != 52; }\n' > "$build/abi.c"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" "$build/abi.c" -o "$build/abi"
"$build/abi"

"${cxx[@]}" -I"$repo/src" "$repo/test/resources/execution_context_test.cpp" -o "$build/execution-context"
"$build/execution-context"

run_plain device-registry "$repo/test/streams/usb_device_registry_test.cpp"
run_plain serial-provider-registry "$repo/test/streams/serial_provider_registry_test.cpp"

"${cxx[@]}" "${inc[@]}" "$repo/test/streams/serial_provider_bridge_test.cpp"   "$build/serial-bridge.o" -o "$build/serial-provider-bridge"
"$build/serial-provider-bridge"

"${cxx[@]}" "${inc[@]}" "$repo/test/streams/usb_semantic_bridge_test.cpp"   "$build/serial-bridge.o" -o "$build/usb-semantic-bridge"
"$build/usb-semantic-bridge"

# Host callbacks only publish snapshots. Owner-task ticks reconcile independent
# of serial calls and deliver lifecycle events to context-owned subscribers.
"${cxx[@]}" "${inc[@]}" "$repo/test/streams/usb_discovery_tick_test.cpp"   "$build/serial-bridge.o" -o "$build/usb-discovery-tick"
"$build/usb-discovery-tick"

# Compile the exported device observation API, independent of data-stream v2.
"${cxx[@]}" "${inc[@]}" "$repo/src/native/NativeDeviceBridge.cpp"   "$repo/test/streams/usb_device_api_test.cpp" "$build/serial-bridge.o"   -o "$build/usb-device-abi"
"$build/usb-device-abi"

"${cxx[@]}" "${stub_inc[@]}" "$repo/test/streams/bridge_test.cpp"   "$build/stream-runtime.o" "$build/stream-bridge-stub.o" "$build/serial-bridge-stub.o"   -o "$build/bridge"
"$build/bridge"

"${cxx[@]}" "${stub_inc[@]}" "$repo/test/streams/usb_direct_ownership_test.cpp"   "$build/stream-runtime.o" "$build/stream-bridge-stub.o" "$build/serial-bridge-stub.o"   -o "$build/usb-direct-ownership"
"$build/usb-direct-ownership"
