#include "runtime/drivers/InstalledSerialInventory.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
const char* ids[] = {"class.one", "class.four"};
bool verified = true, failAcquire = false, failRelease = false, failRecovery = false;
bool firstUncertain = false;
uint64_t firstToken = 41, fourthToken = 44;
unsigned opens = 0, acquired = 0, released = 0, recovery = 0;
uint64_t open(uint64_t device) { ++opens; return device; }
bool configure(uint64_t, uint32_t, uint8_t, uint8_t, uint8_t) { return true; }
bool control(uint64_t, bool, bool) { return true; }
int32_t read(uint64_t, uint8_t*, size_t, uint32_t) { return 0; }
int32_t write(uint64_t, const uint8_t*, size_t, uint32_t) { return 0; }
bool close(uint64_t) { return true; }
int32_t probe(uint64_t) { return 1; }
bool firstSnapshot(risc_serial_device_v1* out, size_t* count) {
  if (!count || firstUncertain) return false;
  const size_t wanted = firstToken ? 1u : 0u;
  if (*count < wanted || (wanted && !out)) { *count = wanted; return false; }
  if (wanted) out[0] = {firstToken, firstToken, RISC_SERIAL_TRANSPORT_USB, {}};
  *count = wanted;
  return true;
}
bool fourthSnapshot(risc_serial_device_v1* out, size_t* count) {
  if (!count) return false;
  const size_t wanted = fourthToken ? 1u : 0u;
  if (*count < wanted || (wanted && !out)) { *count = wanted; return false; }
  if (wanted) out[0] = {fourthToken, fourthToken, RISC_SERIAL_TRANSPORT_USB, {}};
  *count = wanted;
  return true;
}
const risc_serial_port_inventory_v1 first = {
    {{1, sizeof(risc_serial_port_inventory_v1), open, configure, control,
      read, write, close}, probe}, firstSnapshot};
const risc_serial_port_inventory_v1 fourth = {
    {{1, sizeof(risc_serial_port_inventory_v1), open, configure, control,
      read, write, close}, probe}, fourthSnapshot};
}
namespace RuntimeInstalledProviders {
bool prepare() { return verified; }
bool nextProvider(const char* capability, uint32_t api, size_t* cursor,
                  char* id, size_t capacity) {
  assert(capability && !std::strcmp(capability, "serial.port") && api == 1);
  if (!cursor || !id || *cursor >= 2) return false;
  std::snprintf(id, capacity, "%s", ids[(*cursor)++]);
  return true;
}
bool acquire(const char* id, const char* capability, uint32_t api, Lease* out) {
  assert(id && capability && !std::strcmp(capability, "serial.port") &&
         api == 1 && out);
  *out = {};
  ++acquired;
  if (failAcquire) return false;
  if (!std::strcmp(id, ids[0]))
    *out = {{1, acquired}, &first.discovery.serial};
  else if (!std::strcmp(id, ids[1]))
    *out = {{2, acquired}, &fourth.discovery.serial};
  return out->grant.slot != 0;
}
bool release(Lease* lease) {
  assert(lease && lease->grant.slot);
  ++released;
  if (failRelease) return false;
  *lease = {};
  return true;
}
bool recoverFailedProvider(const char* id, const char* capability, uint32_t api) {
  assert(id && capability && !std::strcmp(capability, "serial.port") && api == 1);
  ++recovery;
  return !failRecovery;
}
}
int main() {
  using RuntimeInstalledProviders::InstalledSerialInventory;
  RuntimeDevices::Registry registry;
  InstalledSerialInventory monitor(registry);
  const risc_serial_port_api_v1* port = nullptr;
  uint64_t token = 99, generation = 99;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Updated);
  assert(monitor.retainedProviders() == 2 && registry.count() == 2 &&
         acquired == 2 && !released && !opens);
  RuntimeDevices::DeviceHandle handles[2]{};
  for (size_t i = 0; i < RuntimeDevices::kMaxDevices; ++i) {
    RuntimeDevices::DeviceInfo item{};
    if (!registry.at(i, &item)) continue;
    if (std::strcmp(item.provider, ids[0]) == 0) handles[0] = item.handle;
    if (std::strcmp(item.provider, ids[1]) == 0) handles[1] = item.handle;
  }
  assert(handles[0] && handles[1] && handles[0] != handles[1]);
  assert(monitor.resolve(handles[0], &port, &token, &generation) &&
         port == &first.discovery.serial && token == 41 && generation == 41);
  assert(monitor.resolve(handles[1], &port, &token, &generation) &&
         port == &fourth.discovery.serial && token == 44 && generation == 44);
  RuntimeDevices::LeaseHandle grant = 0;
  assert(registry.acquire("serial.port", 7, &grant, handles[0],
                          RuntimeDevices::Mode::Exclusive) == RuntimeDevices::Result::Ok);
  firstUncertain = true;
  firstToken = 0;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Uncertain);
  assert(registry.valid(grant, 7) && registry.count() == 2);
  firstUncertain = false;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Updated);
  assert(!registry.valid(grant, 7) && registry.count() == 1);
  assert(!monitor.resolve(handles[0], &port, &token, &generation) &&
         !port && !token && !generation);
  firstToken = 45;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Updated &&
         registry.count() == 2);
  assert(!monitor.resolve(handles[0], &port, &token, &generation));

  // A failed physical release retains the exact class grant, withdraws
  // publications and bars any further discovery until checked retry.
  failRelease = true;
  assert(!monitor.stopChecked() && monitor.faulted() &&
         monitor.retainedProviders() == 2 && !registry.count());
  const unsigned before = acquired;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Fault &&
         acquired == before);
  failRelease = false;
  assert(monitor.stopChecked() && !monitor.faulted() &&
         !monitor.retainedProviders());
  assert(monitor.refresh() == InstalledSerialInventory::Result::Updated &&
         monitor.retainedProviders() == 2 && registry.count() == 2);
  assert(monitor.stopChecked());

  // Failure WITHOUT a grant cannot be repaired by selecting class.four.
  failAcquire = true;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Fault &&
         monitor.faulted() && monitor.retainedProviders() == 1);
  failRecovery = true;
  assert(!monitor.stopChecked() && monitor.faulted() && recovery == 1);
  failRecovery = false;
  assert(monitor.stopChecked() && recovery == 2 && !monitor.faulted());
  failAcquire = false;
  assert(monitor.refresh() == InstalledSerialInventory::Result::Updated);
  assert(monitor.stopChecked());
  std::puts("Installed serial inventory retains exact ELF grants, publishes and recovers: PASS");
}
