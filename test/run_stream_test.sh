#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
common=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
  -I"$repo/lib/NativeApps/include" -I"$repo/src" -I"$repo/sdk/driver")
stream="$repo/src/runtime/streams/StreamRuntime.cpp"
serial="$repo/src/native/NativeSerialPortBridge.cpp"
# Successful-close and transport fakes are host-only. Keep both OUT of the
# production class-binding test, which links NativeUsbClassBridge.cpp itself.
checked="$repo/test/streams/stubs/native_usb_checked_stop_stub.cpp"
bridge_io="$repo/test/streams/stubs/native_usb_bridge_io_stub.cpp"

compile_run() {
  local name="$1"
  shift
  c++ "${common[@]}" "$@" -o "$build/$name"
  "$build/$name"
}
compile_run test "$stream" "$repo/test/streams/runtime_test.cpp"
compile_run async-pipes "$stream" "$repo/test/streams/async_pipe_test.cpp"
compile_run elf-endpoints "$stream" "$repo/test/streams/elf_endpoint_test.cpp"
compile_run record-queue "$repo/test/streams/record_queue_test.cpp"
compile_run record-registry "$stream" "$repo/test/streams/record_registry_test.cpp"
compile_run gnss-record "$stream" "$repo/test/streams/gnss_record_adapter_test.cpp"
compile_run location-subscriptions "$stream" "$repo/test/streams/location_position_subscriptions_test.cpp"
compile_run gnss-producer "$stream" "$repo/test/streams/cooperative_gnss_producer_test.cpp"
compile_run location-lease-binding "$stream" "$repo/test/streams/location_lease_binding_test.cpp"
compile_run location-production-registry "$stream" "$repo/test/streams/location_production_registry_test.cpp"
compile_run http-transfer "$stream" "$repo/test/streams/http_transfer_test.cpp"
printf '#include "T5StreamApi.h"\n#include "T5SerialPortApi.h"\n#include "T5DeviceApi.h"\n#include "RiscRteLocationRecords.h"\nint main(void) { return T5_STREAM_API_VERSION != 1 || T5_SERIAL_PORT_API_VERSION != 1 || T5_DEVICE_API_VERSION != 1 || RISCRTE_LOCATION_FIX_SIZE != 52; }\n' > "$build/abi.c"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" "$build/abi.c" -o "$build/abi"
"$build/abi"
compile_run execution-context "$repo/test/resources/execution_context_test.cpp"
compile_run device-registry "$repo/test/streams/usb_device_registry_test.cpp"
compile_run provider-publication "$repo/test/streams/provider_device_publisher_test.cpp"
compile_run serial-provider-inventory "$repo/test/streams/serial_provider_devices_test.cpp"
compile_run serial-provider-registry "$repo/test/streams/serial_provider_registry_test.cpp"
compile_run serial-structured-diagnostic "$repo/test/streams/serial_structured_diagnostic_test.cpp"
compile_run installed-serial-session -I"$repo/sdk/driver" \
  "$repo/test/drivers/installed_serial_session_test.cpp"
compile_run installed-serial-bridge -DRISCRTE_TEST_INSTALLED_SERIAL_PATH \
  -I"$repo/sdk/driver" "$serial" "$repo/test/streams/installed_serial_bridge_test.cpp"
compile_run serial-failed-acquire "$repo/test/streams/serial_failed_acquire_test.cpp"
compile_run serial-provider-bridge -DRISCRTE_TEST_CLASS_BINDING_PROVIDED "$serial" "$checked" "$bridge_io" "$repo/test/streams/serial_provider_bridge_test.cpp"
compile_run serial-diagnostic-context -DRISCRTE_TEST_CLASS_BINDING_PROVIDED "$serial" "$checked" "$bridge_io" "$repo/test/streams/serial_diagnostic_context_test.cpp"
compile_run usb-semantic-bridge -DRISCRTE_TEST_CLASS_BINDING_PROVIDED "$serial" "$checked" "$bridge_io" "$repo/test/streams/usb_semantic_bridge_test.cpp"
compile_run usb-discovery-tick -DRISCRTE_TEST_CLASS_BINDING_PROVIDED "$serial" "$checked" "$bridge_io" "$repo/test/streams/usb_discovery_tick_test.cpp"
compile_run usb-device-abi -DRISCRTE_TEST_CLASS_BINDING_PROVIDED "$serial" "$checked" "$bridge_io" "$repo/src/native/NativeDeviceBridge.cpp" \
  "$repo/test/streams/usb_device_api_test.cpp"
# These two bridge fixtures intentionally use fake storage/scheduler headers.
production_common=("${common[@]}")
common+=(-I"$repo/test/streams/stubs")
compile_run bridge "$stream" "$repo/src/native/NativeStreamBridge.cpp" "$serial" \
  "$checked" "$bridge_io" "$repo/test/streams/bridge_test.cpp"
compile_run usb-direct-ownership "$stream" "$repo/src/native/NativeStreamBridge.cpp" "$serial" \
  "$checked" "$bridge_io" "$repo/test/streams/usb_direct_ownership_test.cpp"
# Restore original includes before linking the real installed class bridge.
common=("${production_common[@]}")
compile_run usb-class-bridge "$stream" "$serial" "$repo/src/native/NativeUsbClassBridge.cpp" \
  "$repo/test/streams/usb_class_bridge_bind_test.cpp"
compile_run usb-class-session "$stream" "$repo/test/streams/usb_class_stream_session_test.cpp"
