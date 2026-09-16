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
  assert(failing == 0);  // Legacy sidecar imposes no new launch requirement.
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
  const Descriptor gps{"board.gnss.uart0", "GNSS", "gps-nmea", Transport::Uart,
                       gpsCaps, 1, 100};
  DeviceHandle gpsDevice = 0;
  assert(registry.add(gps, State::Unavailable, &gpsDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Unavailable && failing == 0);
  assert(registry.setState(gpsDevice, State::Available));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Missing && failing == 1);

  const char* serialCaps[] = {"serial.port", "serial.host"};
  const Descriptor serial{"usb.session.1", "USB UART", "usb.serial", Transport::Usb,
                          serialCaps, 2, 100};
  DeviceHandle serialDevice = 0;
  assert(registry.add(serial, State::Available, &serialDevice));
  assert(resolveRequirements(registry, required, &failing) == RequirementResult::Ready && failing == required.count);

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

  // A registered capability with an unknown provider-version contract must
  // fail closed. Registry presence alone does not prove compatible APIs.
  const char* customCaps[] = {"sensor.temperature"};
  const Descriptor custom{"sensor.1", "Thermometer", "unknown.elf", Transport::I2c,
                          customCaps, 1, 100};
  DeviceHandle customDevice = 0;
  assert(registry.add(custom, State::Available, &customDevice));
  AppCapabilityRequirements customRequired{};
  assert(addRequirement(&customRequired, "sensor.temperature", ">=1"));
  assert(resolveRequirements(registry, customRequired, &failing) == RequirementResult::UnknownApi);
  for (size_t i = 0; i < kMaxAppRequirements; ++i) {
    char capability[32];
    std::snprintf(capability, sizeof(capability), "test.capability%u", static_cast<unsigned>(i));
    AppCapabilityRequirements full{};
    // Validate the exact capacity boundary separately from name uniqueness.
    for (size_t j = 0; j < kMaxAppRequirements; ++j) {
      char name[32];
      std::snprintf(name, sizeof(name), "test.resource%u", static_cast<unsigned>(j));
      assert(addRequirement(&full, name, ">=1"));
    }
    assert(!addRequirement(&full, capability, ">=1"));
  }
  std::puts("App manifest capability preflight, version and state tests passed");
}
