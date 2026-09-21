#include "runtime/capabilities/SerialProviderRegistry.h"
#include <cassert>
#include <cstdio>

namespace {
struct Simulated {
  bool failAcquire = false;
  bool malformed = false;
  bool failRelease = true;
  int acquired = 0;
  int released = 0;
};
bool available(void*) { return true; }
bool matches(void*, t5_serial_device_t id) { return id == 7; }
t5_serial_result_t acquire(void* context, const t5_serial_port_request_t*,
                           t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  auto& sim = *static_cast<Simulated*>(context);
  ++sim.acquired;
  *lease = 91;
  *rx = 21;
  *tx = sim.malformed ? 21 : 22;
  return sim.failAcquire ? T5_SERIAL_IO : T5_SERIAL_OK;
}
t5_serial_result_t configure(void*, t5_serial_port_lease_t, const t5_serial_config_t*) {
  return T5_SERIAL_OK;
}
t5_serial_result_t status(void*, t5_serial_port_lease_t, t5_serial_port_state_t*) {
  return T5_SERIAL_OK;
}
t5_serial_result_t control(void*, t5_serial_port_lease_t, bool, bool) {
  return T5_SERIAL_OK;
}
t5_serial_result_t release(void* context, t5_serial_port_lease_t token) {
  auto& sim = *static_cast<Simulated*>(context);
  assert(token == 91);
  ++sim.released;
  return sim.failRelease ? T5_SERIAL_IO : T5_SERIAL_OK;
}
RuntimeSerial::Provider provider(Simulated& sim) {
  return {"test.serial", 0, &sim, available, matches, acquire, configure,
          status, control, release};
}
}

int main() {
  t5_serial_port_request_t request{};
  t5_serial_port_lease_t lease = 55;
  t5_stream_t rx = 55, tx = 55;

  // Even a non-OK acquire may hand back a private token. A failed cleanup
  // cannot be forgotten because no caller has received its public handle.
  RuntimeSerial::Registry registry;
  Simulated sim{};
  sim.failAcquire = true;
  assert(registry.add(provider(sim)));
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && registry.leased() && sim.released == 1);
  assert(!registry.remove("test.serial"));
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY);
  assert(sim.acquired == 1);
  registry.end();
  assert(registry.leased() && sim.released == 2);
  sim.failRelease = false;
  registry.end();
  assert(!registry.leased() && sim.released == 3);
  sim.failAcquire = false;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK && lease);
  assert(registry.release(lease) == T5_SERIAL_OK);

  // A successful provider acquire with invalid streams is equally unsafe
  // when cleanup fails. No bogus streams or public lease may escape.
  Simulated bad{};
  bad.malformed = true;
  RuntimeSerial::Registry malformed;
  assert(malformed.add(provider(bad)));
  assert(malformed.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && malformed.leased());
  assert(malformed.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY);
  bad.failRelease = false;
  malformed.end();
  assert(!malformed.leased());
  std::puts("Serial partial-acquisition quarantine and retry tests passed");
}
