#include "runtime/capabilities/AppCapabilityRequirements.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeDevices;

int main() {
  AppCapabilityRequirements required{};
  size_t failing = 99;
  Registry registry;
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Ready);
  assert(failing == 0);
  assert(!validCapabilityName(nullptr));
  assert(!validCapabilityName("Serial.Port"));
  assert(!validCapabilityName("../uart"));
  assert(!validCapabilityName(""));
  assert(validCapabilityName("location.position"));
  uint16_t api = 99;
  assert(!parseMinimumApi("1", &api) && api == 0);
  assert(!parseMinimumApi(">=0", &api));
  assert(!parseMinimumApi(">=01", &api));
  assert(!parseMinimumApi(">=65536", &api));
  assert(!parseMinimumApi(">=2.0", &api));
  assert(parseMinimumApi(">=65535", &api) && api == 65535);
  assert(addRequirement(&required, "location.position", ">=1"));
  assert(!addRequirement(&required, "location.position", ">=1"));
  assert(!addRequirement(&required, "bad/name", ">=1"));
  assert(!addRequirement(&required, "serial.port", ">=0"));
  assert(addRequirement(&required, "serial.port", ">=1"));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Missing && failing == 0);

  const char* gpsCaps[] = {"location.position"};
  const uint16_t gpsApi[] = {1};
  // Provider string intentionally differs from gps-nmea: API metadata must
  // follow the declared capability, not a hard-coded ELF provider name.
  const Descriptor gps{"board.gnss.uart0", "GNSS", "vendor.gps", Transport::Uart,
                       gpsCaps, 1, 100, gpsApi};
  DeviceHandle gpsDevice = 0;
  assert(registry.add(gps, State::Unavailable, &gpsDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Unavailable && failing == 0);
  assert(registry.setState(gpsDevice, State::Available));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Missing && failing == 1);

  const char* serialCaps[] = {"serial.port", "serial.host"};
  const uint16_t serialApi[] = {1, 0};
  const Descriptor serial{"usb.session.1", "USB UART", "thirdparty.cdc", Transport::Usb,
                          serialCaps, 2, 100, serialApi};
  DeviceHandle serialDevice = 0;
  assert(registry.add(serial, State::Available, &serialDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Ready && failing == required.count);
  assert(knownApiVersion(DeviceInfo{}, "serial.port") == 0);
  DeviceInfo serialInfo{};
  assert(registry.get(serialDevice, &serialInfo));
  assert(knownApiVersion(serialInfo, "serial.port") == 1);
  assert(knownApiVersion(serialInfo, "serial.host") == 0);

  required.entries[1].minApi = 2;
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::ApiTooOld && failing == 1);
  required.entries[1].minApi = 1;
  assert(registry.setState(serialDevice, State::Busy));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Unavailable && failing == 1);
  assert(registry.setState(serialDevice, State::Available));
  assert(registry.remove(serialDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Missing && failing == 1);
  assert(registry.add(serial, State::Available, &serialDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Ready);

  // An unversioned provider fails closed even if its name resembles a known
  // driver. A separate, compatible provider can satisfy the same capability.
  const char* customCaps[] = {"sensor.temperature"};
  const Descriptor custom{"sensor.1", "Thermometer", "gps-nmea", Transport::I2c,
                          customCaps, 1, 100};
  DeviceHandle customDevice = 0;
  assert(registry.add(custom, State::Available, &customDevice));
  AppCapabilityRequirements customRequired{};
  assert(addRequirement(&customRequired, "sensor.temperature", ">=1"));
  assert(resolveRequirements(registry, customRequired, &failing) == RequirementResult::UnknownApi);
  const uint16_t customApi[] = {3};
  const Descriptor alternate{"sensor.2", "Thermometer 2", "sensor.elf", Transport::Ble,
                             customCaps, 1, 50, customApi};
  DeviceHandle selected = 0;
  assert(registry.add(alternate, State::Available, &selected));
  DeviceHandle resolved = 0;
  assert(resolveRequirement(registry, customRequired.entries[0], &resolved) == RequirementResult::Ready);
  assert(resolved == selected);
  assert(registry.get(selected, &serialInfo) && knownApiVersion(serialInfo, "sensor.temperature") == 3);
  assert(registry.remove(selected));
  assert(resolveRequirements(registry, customRequired) == RequirementResult::UnknownApi);

  for (size_t i = 0; i < kMaxAppRequirements; ++i) {
    char capability[32];
    std::snprintf(capability, sizeof(capability), "test.capability%u", static_cast<unsigned>(i));
    AppCapabilityRequirements full{};
    for (size_t j = 0; j < kMaxAppRequirements; ++j) {
      char name[32];
      std::snprintf(name, sizeof(name), "test.resource%u", static_cast<unsigned>(j));
      assert(addRequirement(&full, name, ">=1"));
    }
    assert(!addRequirement(&full, capability, ">=1"));
  }
  std::puts("App manifest capability preflight, generic versions and state tests passed");
}
