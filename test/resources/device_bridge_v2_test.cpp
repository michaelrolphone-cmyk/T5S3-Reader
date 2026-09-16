#include <T5AppApi.h>
#include <T5DeviceApi.h>
#include "native/NativeDeviceConsent.h"
#include "runtime/capabilities/CapabilityAccess.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

static bool appSession = false;
static bool consentAllowed = false;
static bool detachDuringPrompt = false;
static bool mutateDuringPrompt = false;
static char mutableCapability[40] = "location.position";
static unsigned promptCount = 0;
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  static const t5_app_api_v1 api{};
  return appSession && version == T5_APP_ABI_VERSION ? &api : nullptr;
}
void nativeDeviceDiscoveryTick() {}  // Snapshot mutation belongs to owner task.
bool nativeDeviceConsentPrompt(const DeviceInfo& device, const char* capability, uint32_t rights) {
  ++promptCount;
  assert(capability && rights && device.handle);
  if (mutateDuringPrompt) {
    mutateDuringPrompt = false;
    assert(std::strcmp(capability, "location.position") == 0);
    std::strcpy(mutableCapability, "serial.port");
    assert(std::strcmp(capability, "location.position") == 0);
  }
  if (detachDuringPrompt) {
    detachDuringPrompt = false;
    assert(systemRegistry().remove(device.handle));
  }
  return consentAllowed;
}

int main() {
  assert(t5_device_get_api(1) == nullptr && t5_device_get_api(2) == nullptr &&
         t5_device_get_api(3) == nullptr);
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
  const auto* api = reinterpret_cast<const t5_device_api_v2*>(prefix);
  const t5_device_api_v1* third = t5_device_get_api(T5_DEVICE_API_VERSION_3);
  assert(third && third->api_version == 3 && third->struct_size == sizeof(t5_device_api_v3));
  const auto* v3 = reinterpret_cast<const t5_device_api_v3*>(third);
  assert(t5_device_get_api(4) == nullptr);
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

  // v3 explicitly invokes firmware-owned consent; v1/v2 never prompt.
  const unsigned firstPrompt = promptCount;
  assert(v3->request("location.position", device, 0, &access) == T5_DEVICE_INVALID);
  assert(v3->request("location.position", device, 8, &access) == T5_DEVICE_INVALID);
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, nullptr) == T5_DEVICE_INVALID);
  assert(v3->request("unsupported.capability", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_INVALID);
  assert(promptCount == firstPrompt);
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) ==
         T5_DEVICE_DENIED && access == 0 && promptCount == firstPrompt + 1);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);
  consentAllowed = true;
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(access && promptCount == firstPrompt + 2);
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_OK && observed == device);
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(promptCount == firstPrompt + 2);  // Already approved rights do not prompt again.
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_WRITE, &access) == T5_DEVICE_OK);
  assert(promptCount == firstPrompt + 3);  // Escalation needs its own explicit decision.
  assert(api->validate(access, T5_DEVICE_RIGHT_WRITE, &observed) == T5_DEVICE_OK);
  assert(systemCapabilityAccess().revokeTrusted(context.id(), device, "location.position"));
  assert(api->validate(access, T5_DEVICE_RIGHT_WRITE, &observed) == T5_DEVICE_STALE);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);

  // ELF-owned mutable text must be copied before the owner task pauses for UI;
  // otherwise a worker can change which capability is granted after consent.
  std::strcpy(mutableCapability, "location.position");
  mutateDuringPrompt = true;
  assert(v3->request(mutableCapability, device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(std::strcmp(mutableCapability, "serial.port") == 0);
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_OK && observed == device);
  assert(systemCapabilityAccess().revokeTrusted(context.id(), device, "location.position"));
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_STALE);

  detachDuringPrompt = true;
  const DeviceHandle disconnected = device;
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_STALE);
  assert(access == 0 && disconnected == device);
  assert(registry.add(desc, State::Available, &device) && device != disconnected);
  assert(api->acquire("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_OK);
  assert(api->validate(access, T5_DEVICE_RIGHT_READ, &observed) == T5_DEVICE_OK && observed == device);

  context.requestStop();
  assert(t5_device_get_api(1) == nullptr && t5_device_get_api(2) == nullptr &&
         t5_device_get_api(3) == nullptr);
  assert(v3->request("location.position", device, T5_DEVICE_RIGHT_READ, &access) == T5_DEVICE_DENIED);
  context.end();
  assert(registry.leaseCount() == 0 && systemCapabilityAccess().owner() == 0);
  appSession = false;
  assert(t5_device_get_api(3) == nullptr);
  std::puts("Device ABI v1/v2 compatibility, v3 scoped consent and immutable request tests passed");
}
