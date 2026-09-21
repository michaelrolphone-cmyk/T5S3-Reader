#include "runtime/capabilities/SerialProviderRegistry.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using RuntimeSerial::Registry;
using RuntimeSerial::Provider;
namespace {
struct Fake {
  t5_serial_device_t device = 0;
  bool online = true, busy = false, malformed = false, failRelease = false;
  int acquired = 0, released = 0, configured = 0, controls = 0;
};
bool available(void* p) { return static_cast<Fake*>(p)->online; }
bool matches(void* p, t5_serial_device_t id) { return static_cast<Fake*>(p)->device == id; }
t5_serial_result_t acquire(void* p, const t5_serial_port_request_t*,
                           t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  auto& f = *static_cast<Fake*>(p);
  ++f.acquired;
  if (f.busy) return T5_SERIAL_BUSY;
  *lease = 11;
  *rx = 21;
  *tx = f.malformed ? 21 : 22;
  return T5_SERIAL_OK;
}
t5_serial_result_t configure(void* p, t5_serial_port_lease_t lease, const t5_serial_config_t*) {
  assert(lease == 11);
  ++static_cast<Fake*>(p)->configured;
  return T5_SERIAL_OK;
}
t5_serial_result_t status(void* p, t5_serial_port_lease_t lease, t5_serial_port_state_t* out) {
  assert(lease == 11 && out);
  *out = {};
  out->device = static_cast<Fake*>(p)->device;
  return T5_SERIAL_OK;
}
t5_serial_result_t control(void* p, t5_serial_port_lease_t lease, bool, bool) {
  assert(lease == 11);
  ++static_cast<Fake*>(p)->controls;
  return T5_SERIAL_OK;
}
t5_serial_result_t release(void* p, t5_serial_port_lease_t lease) {
  assert(lease == 11);
  auto& f = *static_cast<Fake*>(p);
  ++f.released;
  return f.failRelease ? T5_SERIAL_IO : T5_SERIAL_OK;
}
Provider make(const char* name, uint8_t priority, Fake& f) {
  return {name, priority, &f, available, matches, acquire, configure, status, control, release};
}
} // namespace

int main() {
  Registry registry;
  Fake usb{3}, uart{0x80000001u}, colliding{0x80000001u}, fourth{7}, fifth{9};
  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 123;
  t5_stream_t rx = 123, tx = 123;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_UNSUPPORTED);
  assert(!lease && !rx && !tx);
  request.device = 999;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_INVALID);
  assert(!registry.add(Provider{}));
  assert(registry.add(make("usb.serial", 0, usb)));
  assert(!registry.add(make("usb.serial", 0, usb)));
  assert(registry.add(make("uart.serial", 10, uart)));
  request.device = 0;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 21 && tx == 22 && usb.acquired == 1 && !uart.acquired);
  assert(!registry.add(make("third.serial", 20, fourth)));
  assert(!registry.remove("usb.serial"));
  assert(registry.acquire(&request, &rx, &rx, &tx) == T5_SERIAL_BUSY);
  assert(registry.configure(lease, &request.config) == T5_SERIAL_OK && usb.configured == 1);
  t5_serial_port_state_t state{};
  assert(registry.status(lease, &state) == T5_SERIAL_OK && state.device == usb.device);
  assert(registry.control(lease, true, false) == T5_SERIAL_OK && usb.controls == 1);

  // Explicit release failure preserves the live handle for its owner's retry.
  usb.failRelease = true;
  assert(registry.release(lease) == T5_SERIAL_IO && registry.leased());
  t5_serial_port_lease_t refused = 91;
  assert(registry.acquire(&request, &refused, &rx, &tx) == T5_SERIAL_BUSY && !refused);
  assert(!registry.remove("usb.serial"));
  usb.failRelease = false;
  assert(registry.release(lease) == T5_SERIAL_OK && usb.released == 2);
  assert(registry.release(lease) == T5_SERIAL_CLOSED);
  const auto stale = lease;

  usb.online = false;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(uart.acquired == 1 && lease != stale);
  registry.end();
  registry.end();
  assert(uart.released == 1 && registry.configure(lease, &request.config) == T5_SERIAL_CLOSED);

  usb.online = true;
  request.device = uart.device;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(uart.acquired == 2 && usb.acquired == 1);
  assert(registry.release(lease) == T5_SERIAL_OK);
  assert(registry.add(make("colliding.serial", 15, colliding)));
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_INVALID);
  assert(!lease && !rx && !tx && colliding.acquired == 0);
  assert(registry.remove("colliding.serial"));
  request.device = 0;

  usb.busy = true;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY && !lease);
  usb.busy = false;
  usb.malformed = true;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && usb.released == 3);
  usb.malformed = false;
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease != stale);
  registry.end();

  // Context exit is NOT an ordinary failed release: the old invocation's
  // public lease must be revoked immediately, but the private device token
  // must remain pinned and retried without permitting a second provider.
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  const auto exitedLease = lease;
  const int oldAcquired = usb.acquired;
  usb.failRelease = true;
  registry.end();
  assert(registry.leased());
  assert(registry.configure(exitedLease, &request.config) == T5_SERIAL_CLOSED);
  assert(registry.release(exitedLease) == T5_SERIAL_CLOSED);
  assert(!registry.remove("usb.serial"));
  registry.end(); // Retry failed: do not detach or acquire another provider.
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY);
  assert(!lease && !rx && !tx && usb.acquired == oldAcquired);
  usb.failRelease = false;
  registry.end(); // Same private lease can now quiesce successfully.
  assert(!registry.leased());
  assert(registry.acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && lease != exitedLease && usb.acquired == oldAcquired + 1);
  registry.end();
  assert(!registry.leased());

  assert(registry.add(make("fourth.serial", 20, fourth)));
  assert(registry.add(make("fifth.serial", 30, fifth)));
  Fake sixth{11};
  assert(!registry.add(make("sixth.serial", 40, sixth)));
  assert(registry.remove("fourth.serial"));
  assert(registry.add(make("sixth.serial", 40, sixth)));
  assert(!registry.remove("missing.serial"));
  std::puts("Bounded serial provider resolver, context-end quarantine and generation tests passed");
}
