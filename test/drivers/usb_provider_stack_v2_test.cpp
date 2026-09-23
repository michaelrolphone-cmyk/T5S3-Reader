#include "runtime/drivers/ProviderGraphV2.h"
#include <RiscUsbProviderV1.h>
#include <cassert>
#include <cstdio>

#include "serial_prefix_stream_host.h"

int main(int argc, char **argv) {
  assert(argc == 3);
  RuntimeProviders::GraphV2 graph(&streamHost);
  const RuntimeProviders::RequirementV2 needsHost[] = {{"usb.host", 1}};
  assert(graph.addVerified({"fixture-usb-host", argv[1], "usb.host", 1, nullptr, 0}));
  assert(graph.addVerified({"usb-cdc-acm-v2", argv[2], "serial.port", 1,
                            needsHost, 1}));
  // A semantic consumer never needs to identify USB, class or device driver.
  auto serial = graph.acquire("serial.port", 1);
  assert(serial.slot);
  auto *cdc = static_cast<const risc_usb_cdc_api_v1 *>(graph.interfaceFor(serial));
  assert(cdc && cdc->api_version == RISC_USB_CDC_API_V1);
  uint64_t session = cdc->open(42);
  assert(session);
  assert(cdc->configure(session, 115200, 8, 0, 1));
  assert(cdc->control_lines(session, true, false));
  uint8_t bytes[8]{};
  assert(cdc->read(session, bytes, sizeof(bytes), 20) == 2);
  assert(bytes[0] == 'O' && bytes[1] == 'K');
  assert(cdc->write(session, (const uint8_t *)"hello", 5, 20) == 5);
  // The host provider cannot unload while a dependent serial provider lives.
  auto host = graph.acquire("usb.host", 1);
  assert(host.slot && graph.interfaceFor(host));
  assert(!graph.shutdown());
  assert(cdc->close(session));
  assert(graph.release(serial) && !graph.interfaceFor(serial));
  assert(graph.interfaceFor(host));
  assert(graph.release(host) && graph.shutdown());
  // A missing USB host ELF cannot be replaced with a hidden firmware fallback.
  RuntimeProviders::GraphV2 missing;
  assert(missing.addVerified({"usb-cdc-acm-v2", argv[2], "serial.port", 1,
                              needsHost, 1}));
  assert(!missing.acquire("serial.port", 1).slot && missing.shutdown());
  std::puts("Two independently loaded provider ELFs: usb.host -> serial.port PASS");
}
