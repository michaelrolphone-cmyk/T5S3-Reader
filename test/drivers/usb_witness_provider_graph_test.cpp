#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/capabilities/SerialProviderDevices.h"
#include <RiscUsbProviderV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
  assert(argc == 4);
  RuntimeProviders::GraphV2 graph;
  const RuntimeProviders::RequirementV2 hostNeeds[] = {{"usb.controller", 1}};
  const RuntimeProviders::RequirementV2 serialNeeds[] = {{"usb.host", 1}};
  assert(graph.addVerified({"fixture-witness-controller", argv[1],
                            "usb.controller", 1, nullptr, 0}));
  assert(graph.addVerified({"usb-host-v2", argv[2], "usb.host", 1,
                            hostNeeds, 1}));
  assert(graph.addVerified({"usb-serial-witness", argv[3], "serial.port", 1,
                            serialNeeds, 1}));

  // No firmware class table: acquire by semantic capability + installed ID.
  auto serialGrant =
      graph.acquireFrom("usb-serial-witness", "serial.port", RISC_SERIAL_PORT_API_V1);
  assert(serialGrant.slot);
  auto *port = static_cast<const risc_serial_port_api_v1*>(
      graph.interfaceFor(serialGrant));
  assert(port && port->api_version == RISC_SERIAL_PORT_API_V1 &&
         port->struct_size >= sizeof(risc_serial_port_inventory_v1));
  auto *inventory = reinterpret_cast<const risc_serial_port_inventory_v1*>(port);
  assert(inventory->snapshot && inventory->discovery.probe);

  RuntimeDevices::Registry registry;
  RuntimeDevices::SerialProviderDevices publication(registry);
  assert(publication.refresh("usb-serial-witness", port) ==
         RuntimeDevices::SerialProviderDevices::Result::Updated);
  assert(publication.count() == 1 && registry.count() == 1);

  RuntimeDevices::DeviceInfo device{};
  bool found = false;
  for (size_t i = 0; i < RuntimeDevices::kMaxDevices; ++i) {
    if (registry.at(i, &device)) { found = true; break; }
  }
  assert(found && !std::strcmp(device.provider, "usb-serial-witness") &&
         !std::strcmp(device.capabilities[0], "serial.port") &&
         device.transport == RuntimeDevices::Transport::Usb);

  uint64_t providerDevice = 0, generation = 0;
  assert(publication.resolve(device.handle, &providerDevice, &generation));
  assert(providerDevice && generation);

  const uint64_t session = port->open(providerDevice);
  assert(session);
  assert(port->configure(session, 115200, 8, 0, 1));
  assert(port->control_lines(session, true, false));
  uint8_t bytes[8]{};
  assert(port->read(session, bytes, sizeof(bytes), 20) == 3 &&
         !std::memcmp(bytes, "NEW", 3));
  assert(port->write(session, bytes, sizeof(bytes), 20) == 2);
  assert(port->close(session));

  // Publication is withdrawn before the provider graph grant can be released.
  assert(publication.withdrawAll() && registry.count() == 0);
  assert(graph.release(serialGrant));
  assert(graph.shutdown());
  std::puts("Fourth packaged-class provider graph to semantic publication: PASS");
}
