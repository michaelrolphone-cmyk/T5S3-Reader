#include "runtime/drivers/UsbCdcDriverModule.h"
#include <cassert>
#include <cstdint>
#include <iostream>

static const uint8_t config[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,2,0,0,2,2,1,0,
    9,4,3,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0
};
int main(int argc, char** argv) {
  assert(argc == 3);
  UsbCdcDriverModule module;
  t5_usb_cdc_binding_v1 bound{};
  assert(!module.probe(config, sizeof(config), 0, 0, &bound));
  assert(!module.load(nullptr));
  assert(module.state() == UsbCdcDriverModule::State::Absent);
  assert(!module.load(argv[2])); // The GPS driver must not satisfy USB CDC.
  assert(module.state() == UsbCdcDriverModule::State::Failed);
  assert(module.load(argv[1]));
  assert(module.state() == UsbCdcDriverModule::State::Active);
  assert(!module.load(argv[1]));
  assert(module.probe(config, sizeof(config), 0, 0, &bound));
  assert(bound.data_interface == 3 && bound.ep_in == 0x81 && bound.ep_out == 2);
  uint8_t payload[7]{};
  assert(module.lineCoding(115200, 8, 0, 1, payload) && payload[1] == 0xc2);
  uint16_t lines = 0;
  assert(module.controlLines(true, false, &lines) && lines == 1);
  assert(!module.controlLines(true, true, nullptr));
  assert(module.unload());
  assert(module.state() == UsbCdcDriverModule::State::Absent);
  assert(!module.probe(config, sizeof(config), 0, 0, &bound));
  assert(!module.lineCoding(115200, 8, 0, 1, payload));
  assert(module.unload());
  assert(module.load(argv[1]));
  assert(module.unload());
  std::cout << "USB CDC runtime loader tests passed\n";
}
