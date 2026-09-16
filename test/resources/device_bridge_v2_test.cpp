#include <T5AppApi.h>
#include <T5DeviceApi.h>
#include "runtime/capabilities/CapabilityAccess.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

static bool appSession = false;
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 api{};
  return appSession && version == T5_APP_ABI_VERSION ? &api : nullptr;
}
void nativeDeviceDiscoveryTick() {}  // Snapshot mutation belongs to owner task.

int main() {
  assert(t5_device_get_api(1) == nullptr && t5_device_get_api(2) == nullptr);
  Registry& registry = systemRegistry();
  const char* caps[] = {"location.position"};
  const uint16_t version[] = {1};
  const Descriptor desc{"unit.test.location", "Location", "test.provider", Transport::Uart,
                        caps, 1, 10, version};
  DeviceHandle device = 0;
  assert(registry.add(desc, State::Available, &device));
  ExecutionContext context;
  assert(context.begin());
  appSession = true;
  const t5_device_api_v1* old = t5_device_get_api(T5_DEVICE_API_VERSION);
  assert(old && old->api_version == 1 && old->struct_size == sizeof(t5_device_api_v1));
  const t5_device_api_v1* prefix = t5_device_get_api(T5_DEVICE_API_VERSION_2);
  assert(prefix && prefix->api_version == 2 && prefix->struct_size == sizeof(t5_device_api_v2));
  assert(t5_device_get_api(3) == nullptr);
  const auto* api = reinterpret_cast<const t5_device_api_v2*>(prefix);
  t5_device_info_t inventory[kMaxDevices]{};
  uint32_t count = 0;
  assert(api->v1.inventory(inventory, kMaxDevices, &count) == T5_DEVICE_OK && count == 1);
  assert(inventory[0].handle == device);
  t5_device_lease_t access = 123;
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) ==
         T5_DEVICE_DENIED && access == 0);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, nullptr) == T5_DEVICE_INVALID);
  assert(systemCapabilityAccess().grantTrusted(context, device, "location.position", kCapabilityRead));
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_WRITE, &access) ==
         T5_DEVICE_DENIED && access == 0);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(access);
  t5_device_handle_t observed = 333;
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_OK && observed == device);
  assert(api->validate(access, T5_DEVICE_RIGHT_WRITE, &observed) == T5_DEVICE_STALE && observed == 0);
  assert(api->release(access) == T5_DEVICE_OK);
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_STALE);
  assert(api->release(access) == T5_DEVICE_STALE);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(registry.remove(device));
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_STALE && observed == 0);
  assert(api->release(access) == T5_DEVICE_OK);
  assert(registry.add(desc, State::Available, &device));
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);
  assert(access == 0);
  context.requestStop();
  assert(t5_device_get_api(1) == nullptr && t5_device_get_api(2) == nullptr);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);
  context.end();
  assert(registry.leaseCount() == 0 && systemCapabilityAccess().owner() == 0);
  appSession = false;
  assert(t5_device_get_api(2) == nullptr);
  std::puts("Device ABI v2 authenticated bridge and revocation tests passed");
}
