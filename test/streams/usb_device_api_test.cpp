// Reuse the real production USB/serial bridge with a simulated host, and
// exercise the exported C ABI rather than the internal subscription class.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_semantic_bridge_fixture
#include "usb_semantic_bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <T5DeviceApi.h>
#include "runtime/capabilities/DeviceEventSubscriptions.h"

int main() {
  auto& registry = RuntimeDevices::systemRegistry();
  auto& subscriptions = RuntimeDevices::systemEventSubscriptions();
  assert(!t5_device_get_api(T5_DEVICE_API_VERSION));
  assert(!t5_device_get_api(T5_DEVICE_API_VERSION + 1));

  RuntimeResources::ExecutionContext context;
  assert(context.begin());
  nativeSerialPortsBegin();
  const auto* api = t5_device_get_api(T5_DEVICE_API_VERSION);
  assert(api && api->api_version == 1 && api->struct_size == sizeof(*api));
  assert(!t5_device_get_api(0));

  uint32_t count = 99;
  assert(api->inventory(nullptr, 0, &count) == T5_DEVICE_OK && count == 0);
  t5_usb_serial_state_t observed{};
  observed.connected = 1;
  observed.status = T5_USB_STATUS_READY;
  observed.vid = 0x10C4;
  observed.pid = 0xEA60;
  std::strcpy(observed.product, "USB semantic ABI device");
  nativeUsbProviderAttach(&observed, 2);
  assert(registry.count() == 0);  // Callback does not enter unified registry.
  assert(api->inventory(nullptr, 0, &count) == T5_DEVICE_LIMIT && count == 1);
  assert(api->inventory(nullptr, 1, &count) == T5_DEVICE_INVALID);
  t5_device_info_t devices[2]{};
  assert(api->inventory(devices, 2, &count) == T5_DEVICE_OK && count == 1);
  const auto first = devices[0].handle;
  assert(first && devices[0].transport == T5_DEVICE_TRANSPORT_USB &&
         devices[0].state == T5_DEVICE_AVAILABLE &&
         std::strcmp(devices[0].provider, "usb.serial") == 0 &&
         std::strcmp(devices[0].capabilities[0], "serial.port") == 0);

  t5_device_subscription_t sub = 0;
  assert(api->subscribe(&sub) == T5_DEVICE_OK && sub && subscriptions.count() == 1);
  assert(api->snapshot(sub, nullptr, 0, &count) == T5_DEVICE_LIMIT && count == 1);
  assert(api->snapshot(sub, devices, 2, &count) == T5_DEVICE_OK && count == 1);
  t5_device_event_t event{};
  uint64_t missed = 123;
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_EMPTY && missed == 0);

  RuntimeDevices::LeaseHandle lease = 0;
  assert(registry.acquire("serial.port", context.id(), &lease, first,
                          RuntimeDevices::Mode::Exclusive) == RuntimeDevices::Result::Ok);
  nativeUsbProviderDetach();
  assert(registry.leaseCount() == 1);  // No registry mutation in host callback.
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_NEXT &&
         event.kind == T5_DEVICE_CAPABILITY_LOST && event.device == first &&
         event.revoked_leases == 1 && registry.leaseCount() == 0);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_NEXT &&
         event.kind == T5_DEVICE_REMOVAL && event.device == first);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_EMPTY);

  nativeUsbProviderAttach(&observed, 2);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_NEXT && event.kind == T5_DEVICE_ADDED);
  assert(event.device != first);
  assert(api->inventory(devices, 2, &count) == T5_DEVICE_OK && count == 1 &&
         devices[0].handle == event.device);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_EMPTY);

  // Overwrite the bounded journal without polling, then require an atomic
  // inventory resnapshot. A too-small buffer cannot silently clear GAP.
  for (int i = 0; i < 16; ++i) {
    nativeUsbProviderDetach();
    nativeDeviceDiscoveryTick();
    nativeUsbProviderAttach(&observed, 2);
    nativeDeviceDiscoveryTick();
  }
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_GAP && missed > 0);
  assert(api->snapshot(sub, nullptr, 0, &count) == T5_DEVICE_LIMIT && count == 1);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_GAP);
  assert(api->snapshot(sub, devices, 2, &count) == T5_DEVICE_OK && count == 1);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_EMPTY);
  assert(api->unsubscribe(sub) == T5_DEVICE_OK);
  assert(api->poll(sub, &event, &missed) == T5_DEVICE_STALE);
  assert(subscriptions.count() == 0);

  t5_device_subscription_t abandoned = 0;
  assert(api->subscribe(&abandoned) == T5_DEVICE_OK && abandoned != sub);
  nativeSerialPortsEnd();
  context.end();
  assert(!t5_device_get_api(T5_DEVICE_API_VERSION) && subscriptions.count() == 0);
  assert(registry.count() == 0 && registry.leaseCount() == 0);

  RuntimeResources::ExecutionContext next;
  assert(next.begin());
  nativeSerialPortsBegin();
  api = t5_device_get_api(T5_DEVICE_API_VERSION);
  assert(api && api->poll(abandoned, &event, &missed) == T5_DEVICE_STALE);
  nativeSerialPortsEnd();
  next.end();
  std::puts("Device ELF ABI authorization, inventory, gap and reconnect tests passed");
  return 0;
}
