#include "runtime/capabilities/UsbSerialProjection.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  using namespace RuntimeDevices;
  Registry registry;
  UsbSerialProjection usb(registry);
  uint64_t cursor = registry.cursor();
  Event event{};
  assert(usb.reconcile(0, 0, false, 0, 0, 0xff, nullptr));
  assert(usb.device() == 0 && registry.count() == 0);
  // The USB host may be running before asynchronous enumeration completes.
  assert(usb.reconcile(0, 0, false, 0, 0, 0xff, nullptr));
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  assert(usb.reconcile(1, 3, true, 0x10C4, 0xEA60, 1, "USB UART"));
  const DeviceHandle first = usb.device();
  DeviceInfo info{};
  assert(first && registry.get(first, &info));
  assert(info.transport == Transport::Usb && info.capabilityCount == 2);
  assert(std::strcmp(info.label, "USB UART") == 0);
  assert(std::strcmp(info.provider, "usb.serial") == 0);
  assert(registry.count() == 1);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::Added && event.device == first);
  assert(std::strcmp(event.identity, "usb.serial.session.00000003") == 0);
  assert(usb.reconcile(1, 3, true, 0x10C4, 0xEA60, 1, "USB UART"));
  assert(usb.device() == first && registry.count() == 1);
  assert(registry.poll(&cursor, &event) == PollResult::Empty);

  LeaseHandle oldLease = 0;
  assert(registry.acquire("serial.port", 81, &oldLease, first, Mode::Exclusive) == Result::Ok);
  assert(registry.valid(oldLease, 81));
  LeaseHandle denied = 123;
  assert(registry.acquire("serial.host", 82, &denied, first) == Result::Busy && !denied);
  // Same model and VID/PID are NOT grounds to reuse an old enumeration token.
  assert(usb.reconcile(2, 0, false, 0, 0, 0xff, nullptr));
  assert(!registry.valid(oldLease, 81) && registry.leaseCount() == 0);
  assert(!registry.get(first, &info) && usb.device() == 0);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::CapabilityLost && event.device == first &&
         event.current == State::Removed && event.revokedLeases == 1);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::Removed && event.device == first &&
         std::strcmp(event.identity, "usb.serial.session.00000003") == 0);
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  assert(usb.reconcile(2, 5, true, 0x10C4, 0xEA60, 1, "USB UART"));
  const DeviceHandle second = usb.device();
  assert(second && second != first);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::Added && event.device == second &&
         std::strcmp(event.identity, "usb.serial.session.00000005") == 0);
  assert(registry.acquire("serial.port", 82, &denied, first) == Result::NotFound);
  assert(registry.acquire("serial.port", 82, &denied, second) == Result::Ok);
  assert(registry.valid(denied, 82));
  // Product strings come from untrusted descriptors; sanitize and bound them.
  assert(usb.reconcile(3, 7, true, 0x1234, 0x5678, 0, "bad\x01name"));
  assert(!registry.valid(denied, 82));
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::CapabilityLost && event.device == second &&
         event.revokedLeases == 1);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::Removed && event.device == second);
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.kind == EventKind::Added && event.device == usb.device());
  assert(registry.get(usb.device(), &info));
  assert(std::strcmp(info.label, "bad?name") == 0);
  assert(usb.clear() && registry.count() == 0 && registry.leaseCount() == 0);
  assert(usb.reconcile(4, 9, true, 0x1111, 0x2222, 0, ""));
  assert(registry.get(usb.device(), &info));
  assert(std::strcmp(info.label, "USB Serial 1111:2222") == 0);
  assert(usb.clear());
  std::puts("USB serial projection, enumeration lease and lifecycle event tests passed");
}
