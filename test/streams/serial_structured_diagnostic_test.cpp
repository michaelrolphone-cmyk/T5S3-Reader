#include "runtime/capabilities/SerialProviderRegistry.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
struct Sim {
  bool fail = true;
  bool malformed = false;
  int probes = 0;
  int releases = 0;
};
bool available(void*) { return true; }
bool matches(void*, t5_serial_device_t id) { return id == 87; }
t5_serial_result_t acquire(void* context, const t5_serial_port_request_t*,
                           t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  auto& sim = *static_cast<Sim*>(context);
  if (sim.fail) return T5_SERIAL_IO;
  *lease = 17;
  *rx = 21;
  *tx = sim.malformed ? 21 : 22;
  return T5_SERIAL_OK;
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
  assert(token == 17);
  ++static_cast<Sim*>(context)->releases;
  return T5_SERIAL_OK;
}
bool diagnostic(void* context, t5_serial_diagnostic_t* out) {
  if (!out) return false;
  auto& sim = *static_cast<Sim*>(context);
  ++sim.probes;
  out->result = T5_SERIAL_OK; // A provider cannot override the actual result.
  out->provider_error = -1220;
  std::strcpy(out->detail, "provider: physical start failure");
  return true;
}
}

int main() {
  RuntimeSerial::Registry registry;
  Sim sim{};
  RuntimeSerial::Provider provider{"simulated.serial", 3, &sim, available,
      matches, acquire, configure, status, control, release, diagnostic};
  assert(registry.add(provider));
  t5_serial_port_request_t request{};
  t5_serial_port_lease_t lease = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && sim.probes == 1);
  RuntimeSerial::Registry::Failure failure{};
  assert(registry.lastFailure(&failure));
  assert(failure.diagnostic.result == T5_SERIAL_IO);
  assert(failure.diagnostic.provider_error == -1220);
  assert(std::strcmp(failure.provider, "simulated.serial") == 0);
  assert(std::strcmp(failure.diagnostic.detail, "provider: physical start failure") == 0);

  // A malformed public pair remains an error, even if the private provider
  // claims success and reports a different result in its diagnostic callback.
  sim.fail = false;
  sim.malformed = true;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(registry.lastFailure(&failure) && failure.diagnostic.result == T5_SERIAL_IO);
  assert(sim.releases == 1 && !registry.leased());

  // Recovery clears the prior failure; it must never leak into a new session.
  sim.malformed = false;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 21 && tx == 22 && !registry.lastFailure(&failure));
  assert(registry.release(lease) == T5_SERIAL_OK);
  std::puts("Structured semantic serial provider diagnostics and recovery tests passed");
}
