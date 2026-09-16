// Exercise the real USB bridge through its independent owner-task discovery
// entry point. Host callbacks must not mutate the unified device registry.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_semantic_bridge_fixture
#include "usb_semantic_bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/capabilities/DeviceEventSubscriptions.h"

int main() {
  auto& registry = RuntimeDevices::systemRegistry();
  auto& subscriptions = RuntimeDevices::systemEventSubscriptions();
  assert(!RuntimeResources::ExecutionContext::current());
  assert(registry.count() == 0 && subscriptions.count() == 0);

  t5_usb_serial_state_t observed{};
  observed.connected = 1;
  observed.status = T5_USB_STATUS_READY;
  observed.vid = 0x10C4;
  observed.pid = 0xEA60;
  std::strcpy(observed.product, "Discovery-only USB UART");
  const uint64_t initial = registry.cursor();
  nativeUsbProviderAttach(&observed, 2);
  assert(registry.count() == 0 && registry.cursor() == initial);
  nativeDeviceDiscoveryTick();  // No app or serial API has been started.
  assert(registry.count() == 1 && registry.cursor() == initial + 1);
  RuntimeDevices::DeviceInfo first{};
  bool found = false;
  for (size_t i = 0; i < RuntimeDevices::kMaxDevices; ++i) {
    if (registry.at(i, &first)) { found = true; break; }
  }
  assert(found && first.transport == RuntimeDevices::Transport::Usb);
  nativeDeviceDiscoveryTick();
  assert(registry.cursor() == initial + 1);  // Same snapshot produces no duplicates.

  RuntimeResources::ExecutionContext context;
  assert(context.begin());
  RuntimeDevices::SubscriptionHandle sub = 0;
  assert(subscriptions.subscribe(context, &sub) == RuntimeDevices::ObserveResult::Ok);
  RuntimeDevices::LeaseHandle physical = 0;
  assert(registry.acquire("serial.port", context.id(), &physical, first.handle,
                          RuntimeDevices::Mode::Exclusive) == RuntimeDevices::Result::Ok);
  assert(registry.valid(physical, context.id()));

  nativeUsbProviderDetach();
  assert(registry.count() == 1 && registry.valid(physical, context.id()));
  nativeDeviceDiscoveryTick();
  assert(registry.count() == 0 && registry.leaseCount() == 0);
  RuntimeDevices::Event event{};
  assert(subscriptions.poll(sub, context.id(), &event) == RuntimeDevices::ObserveResult::Next);
  assert(event.kind == RuntimeDevices::EventKind::CapabilityLost &&
         event.device == first.handle && event.revokedLeases == 1);
  assert(subscriptions.poll(sub, context.id(), &event) == RuntimeDevices::ObserveResult::Next);
  assert(event.kind == RuntimeDevices::EventKind::Removed && event.device == first.handle);
  assert(subscriptions.poll(sub, context.id(), &event) == RuntimeDevices::ObserveResult::Empty);

  // Identical descriptor must produce a different generation-safe identity.
  nativeUsbProviderAttach(&observed, 2);
  assert(registry.count() == 0);
  nativeDeviceDiscoveryTick();
  RuntimeDevices::DeviceInfo second{};
  found = false;
  for (size_t i = 0; i < RuntimeDevices::kMaxDevices; ++i) {
    if (registry.at(i, &second)) { found = true; break; }
  }
  assert(found && second.handle != first.handle && !registry.valid(physical, context.id()));
  assert(subscriptions.poll(sub, context.id(), &event) == RuntimeDevices::ObserveResult::Next);
  assert(event.kind == RuntimeDevices::EventKind::Added && event.device == second.handle);
  assert(subscriptions.poll(sub, context.id(), &event) == RuntimeDevices::ObserveResult::Empty);

  context.end();
  assert(subscriptions.count() == 0);
  nativeUsbProviderDetach();
  nativeDeviceDiscoveryTick();
  assert(registry.count() == 0 && registry.leaseCount() == 0);
  std::puts("Owner-task USB discovery and lifecycle subscription tests passed");
  return 0;
}
