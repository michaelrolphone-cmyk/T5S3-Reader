// Exercise the real native device observation bridge with the ESP32-only
// passive GNSS discovery path enabled, but no UART, SD, display or GPS ELF.
// Only GpsDriverRuntime::available() and the host clock are replaced.
#include <T5AppApi.h>
#include <T5DeviceApi.h>
#include "native/NativeDeviceConsent.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

uint32_t fakeTime = 0;

namespace {
uint32_t scans = 0;
uint32_t usbDiscoveryTicks = 0;
bool packageAvailable = true;
RuntimeDevices::DeviceHandle receiver = 0;
}

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 app{};
  return version == T5_APP_ABI_VERSION ? &app : nullptr;
}

void nativeDeviceDiscoveryTick() { ++usbDiscoveryTicks; }

bool nativeDeviceConsentPrompt(const RuntimeDevices::DeviceInfo&, const char*, uint32_t) {
  assert(false && "observation must not prompt for hardware access");
  return false;
}

namespace GpsDriverRuntime {
bool available() {
  ++scans;
  auto& devices = RuntimeDevices::systemRegistry();
  if (!receiver) {
    constexpr const char* caps[] = {"location.position"};
    const RuntimeDevices::Descriptor desc{
        "board.gnss.uart0", "GNSS", "gps-nmea", RuntimeDevices::Transport::Uart,
        caps, 1, 100};
    assert(devices.add(desc, packageAvailable ? RuntimeDevices::State::Available
                                               : RuntimeDevices::State::Unavailable,
                       &receiver));
  } else {
    assert(devices.setState(receiver, packageAvailable ? RuntimeDevices::State::Available
                                                       : RuntimeDevices::State::Unavailable));
  }
  return packageAvailable;
}
}

int main() {
  using namespace RuntimeDevices;
  using RuntimeResources::ExecutionContext;
  assert(t5_device_get_api(T5_DEVICE_API_VERSION) == nullptr);
  ExecutionContext context;
  assert(context.begin());
  const t5_device_api_v1* api = t5_device_get_api(T5_DEVICE_API_VERSION);
  assert(api && scans == 0);

  // Inventory discovers the installed GNSS package without a legacy GPS call,
  // permission prompt or hardware claim. Subsequent device polling is cheap.
  uint32_t count = 0;
  assert(api->inventory(nullptr, 0, &count) == T5_DEVICE_LIMIT && count == 1);
  assert(scans == 1 && receiver && systemRegistry().leaseCount() == 0);
  t5_device_info_t entries[12]{};
  assert(api->inventory(entries, 12, &count) == T5_DEVICE_OK && count == 1);
  assert(entries[0].handle == receiver && entries[0].state == T5_DEVICE_AVAILABLE &&
         entries[0].transport == T5_DEVICE_TRANSPORT_UART &&
         std::strcmp(entries[0].provider, "gps-nmea") == 0 &&
         std::strcmp(entries[0].capabilities[0], "location.position") == 0);
  assert(scans == 1);

  t5_device_subscription_t subscription = 0;
  assert(api->subscribe(&subscription) == T5_DEVICE_OK && subscription);
  t5_device_event_t event{};
  uint64_t missed = 0;
  packageAvailable = false;
  fakeTime = 9999;
  assert(api->poll(subscription, &event, &missed) == T5_DEVICE_EMPTY && scans == 1);
  fakeTime = 10000;
  assert(api->poll(subscription, &event, &missed) == T5_DEVICE_NEXT && scans == 2 &&
         event.device == receiver && event.kind == T5_DEVICE_STATE_CHANGED &&
         event.current == T5_DEVICE_UNAVAILABLE);
  assert(api->poll(subscription, &event, &missed) == T5_DEVICE_NEXT &&
         event.kind == T5_DEVICE_CAPABILITY_LOST && scans == 2);
  assert(api->inventory(entries, 12, &count) == T5_DEVICE_OK &&
         entries[0].state == T5_DEVICE_UNAVAILABLE && scans == 2);

  packageAvailable = true;
  fakeTime = 20000;
  assert(api->poll(subscription, &event, &missed) == T5_DEVICE_NEXT && scans == 3 &&
         event.device == receiver && event.kind == T5_DEVICE_STATE_CHANGED &&
         event.current == T5_DEVICE_AVAILABLE);
  assert(api->poll(subscription, &event, &missed) == T5_DEVICE_EMPTY);

  // Elapsed scanning must remain valid over the 32-bit millis() wrap.
  fakeTime = UINT32_MAX - 5u;
  assert(api->inventory(entries, 12, &count) == T5_DEVICE_OK && scans == 4);
  fakeTime = 5;
  assert(api->inventory(entries, 12, &count) == T5_DEVICE_OK && scans == 4);
  fakeTime = 9995;
  assert(api->inventory(entries, 12, &count) == T5_DEVICE_OK && scans == 5);
  assert(usbDiscoveryTicks > scans && systemRegistry().leaseCount() == 0);
  assert(api->unsubscribe(subscription) == T5_DEVICE_OK);
  context.end();
  assert(systemRegistry().leaseCount() == 0 && systemRegistry().remove(receiver));
  std::puts("GNSS passive discovery, package events and wrap-safe cadence passed");
}
