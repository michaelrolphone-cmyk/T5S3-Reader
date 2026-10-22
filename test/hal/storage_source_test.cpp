#include <HalStorage.h>
#include <cassert>
#include <iostream>

int main() {
  // Exact production tracker boundaries without exhausting 64-bit counters.
  risc_storage_volume_api_v1 api{};
  StorageSourceLease lease;
  StorageSourceTracker epochExhausted(UINT64_MAX);
  assert(!epochExhausted.canChange() && !epochExhausted.changed());
  StorageSourceTracker tokenExhausted(1, UINT64_MAX);
  assert(!tokenExhausted.capture(&api, lease) && lease.empty());
  StorageSourceTracker finalToken(1, UINT64_MAX - 1);
  assert(finalToken.capture(&api, lease) && lease.token == UINT64_MAX);
  assert(finalToken.release(lease, &api));
  assert(!finalToken.capture(&api, lease) && lease.empty());
  StorageSourceTracker finalEpoch(UINT64_MAX - 1);
  assert(finalEpoch.canChange() && finalEpoch.changed());
  assert(!finalEpoch.canChange() && !finalEpoch.changed());
  assert(finalEpoch.capture(&api, lease));
  auto changedContext = api;
  int context = 0; api.context = &context;
  assert(!finalEpoch.matches(lease, &api) && !finalEpoch.release(lease, &api));
  api = changedContext;
  assert(finalEpoch.release(lease, &api));

  // Full production SdFat backend: capture remains an empty no-op, does not
  // fence existing remount/sleep behavior, and requires no selected session.
  assert(Storage.captureNativeSource(lease) && lease.empty());
  assert(Storage.matchesNativeSource(lease) && Storage.bindNativeSourceOwner(lease, 7) && lease.empty());
  assert(Storage.begin());
  const auto initial = Storage.generation();
  assert(Storage.captureNativeSource(lease) && Storage.unchanged(initial));
  assert(Storage.writeFile("/legacy-source", "legacy bytes"));
  assert(Storage.readFile("/legacy-source") == "legacy bytes");
  assert(Storage.begin());
  assert(Storage.prepareForSleep() && Storage.cancelSleep() && Storage.commitSleep());
  assert(Storage.releaseNativeSource(lease) && lease.empty());
  auto invalid = lease; invalid.token = 1;
  assert(!Storage.captureNativeSource(invalid) && !Storage.matchesNativeSource(invalid));
  assert(!Storage.bindNativeSourceOwner(invalid, 7) && !Storage.releaseNativeSource(invalid));
  assert(invalid.token == 1);
  std::cout << "Source identity counter exhaustion and production legacy no-op behavior PASS\n";
}
