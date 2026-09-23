#include <RiscUsbControllerV1.h>
#include "runtime/drivers/ProviderModuleV2.h"
#include <cassert>
#include "serial_prefix_stream_host.h"
#include <cstdint>
#include <cstdio>

namespace {
bool configuration(void*, uint64_t, uint8_t*, size_t*, uint16_t*, uint16_t*) { return false; }
bool claim(void*, uint64_t, uint8_t, uint8_t, uint64_t*) { return false; }
void release(void*, uint64_t) {}
int32_t control(void*, uint64_t, uint8_t, uint8_t, uint16_t, uint16_t,
                uint8_t*, uint16_t, uint32_t) { return -1; }
int32_t read(void*, uint64_t, uint8_t, uint8_t*, size_t, uint32_t) { return -1; }
int32_t write(void*, uint64_t, uint8_t, const uint8_t*, size_t, uint32_t) { return -1; }
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  risc_usb_host_discovery_v1 api = {
      {RISC_USB_HOST_API_V1, sizeof(api), nullptr,
       configuration, claim, release, control, read, write},
      [](void*, size_t, size_t*) { return false; },
      [](void*, uint64_t*, size_t*) { return false; },
      [](void*, uint64_t) { return true; }, control};
  risc_provider_dependency_v1 dep = {"usb.host", RISC_USB_HOST_API_V1, &api};
  RuntimeProviders::ModuleV2 module;
  assert(module.setStreamHost(&streamHost));
  assert(!module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 1, nullptr, 1));
  assert(module.state() == RuntimeProviders::ModuleV2::State::Absent);
  assert(!module.load(argv[1], "wrong-id", "serial.port", 1, &dep, 1));
  assert(module.state() == RuntimeProviders::ModuleV2::State::Failed);
  assert(!module.load(argv[1], "usb-cdc-acm-v2", "wrong-capability", 1, &dep, 1));
  assert(!module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 2, &dep, 1));
  risc_provider_dependency_v1 wrong = {"usb.host", 1, nullptr};
  assert(!module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 1, &wrong, 1));
  assert(!module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 1, nullptr, 0));
  assert(module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 1, &dep, 1));
  assert(module.state() == RuntimeProviders::ModuleV2::State::Active);
  assert(module.capability() && module.pinConsumer() && module.consumers() == 1);
  assert(!module.unload());
  assert(module.unpinConsumer() && !module.unpinConsumer());
  assert(module.unload() && module.capability() == nullptr);
  assert(module.state() == RuntimeProviders::ModuleV2::State::Absent);
  assert(module.load(argv[1], "usb-cdc-acm-v2", "serial.port", 1, &dep, 1));
  assert(module.unload());
  std::puts("Generic ABI-v2 module dependency/pin/unload tests: PASS");
  return 0;
}
