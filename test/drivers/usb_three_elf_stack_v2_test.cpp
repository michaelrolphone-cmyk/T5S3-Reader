#include "runtime/drivers/ProviderGraphV2.h"
#include <RiscUsbControllerV1.h>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  assert(argc == 4);
  const RuntimeProviders::RequirementV2 needsController[] = {{"usb.controller", 1}};
  const RuntimeProviders::RequirementV2 needsHost[] = {{"usb.host", 1}};
  RuntimeProviders::GraphV2 graph;
  assert(graph.addVerified({"fixture-usb-controller", argv[1],
                            "usb.controller", 1, nullptr, 0}));
  assert(graph.addVerified({"usb-host-v2", argv[2], "usb.host", 1,
                            needsController, 1}));
  assert(graph.addVerified({"usb-cdc-acm-v2", argv[3], "serial.port", 1,
                            needsHost, 1}));
  auto hostGrant = graph.acquire("usb.host", 1);
  assert(hostGrant.slot);
  auto *host = static_cast<const risc_usb_host_api_v1 *>(graph.interfaceFor(hostGrant));
  assert(host && host->struct_size >= sizeof(risc_usb_host_discovery_v1));
  auto *discovery = static_cast<const risc_usb_host_discovery_v1 *>(host);
  size_t processed = 0;
  assert(discovery->poll(host->context, 8, &processed) && processed == 1);
  uint64_t devices[RISC_USB_HOST_MAX_DEVICES]{};
  size_t capacity = RISC_USB_HOST_MAX_DEVICES;
  assert(discovery->devices(host->context, devices, &capacity) && capacity == 1);
  const uint64_t device = devices[0];
  assert(device && device != 77);

  auto serialGrant = graph.acquire("serial.port", 1);
  assert(serialGrant.slot);
  auto *cdc = static_cast<const risc_usb_cdc_api_v1 *>(graph.interfaceFor(serialGrant));
  assert(cdc && cdc->api_version == 1);
  const uint64_t session = cdc->open(device);
  assert(session);
  assert(cdc->configure(session, 115200, 8, 0, 1));
  assert(cdc->control_lines(session, true, true));
  uint8_t bytes[8]{};
  assert(cdc->read(session, bytes, sizeof(bytes), 20) == 2);
  assert(bytes[0] == 'O' && bytes[1] == 'K');
  assert(cdc->write(session, (const uint8_t *)"TX", 2, 20) == 2);
  assert(!graph.shutdown());
  assert(cdc->close(session));
  assert(graph.release(serialGrant));
  assert(!graph.interfaceFor(serialGrant));
  assert(graph.interfaceFor(hostGrant));
  assert(graph.release(hostGrant) && graph.shutdown());

  // No controller ELF => no host or class functionality. No firmware fallback.
  RuntimeProviders::GraphV2 absent;
  assert(absent.addVerified({"usb-host-v2", argv[2], "usb.host", 1,
                             needsController, 1}));
  assert(absent.addVerified({"usb-cdc-acm-v2", argv[3], "serial.port", 1,
                             needsHost, 1}));
  assert(!absent.acquire("serial.port", 1).slot && absent.shutdown());
  std::puts("Three ELFs: simulated controller -> host discovery -> CDC class PASS");
}
