#include "runtime/drivers/ProviderGraphV2.h"
#include <RiscUsbVbusV1.h>
#include <RiscBq25896ProfileV1.h>
#include <RiscI2cBusV1.h>
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  assert(argc == 5);  // Mock I2C, REAL clock, BQ25896 and T5 profile ELFs.
  const RuntimeProviders::RequirementV2 needs[] = {
    {"i2c.bus", RISC_I2C_BUS_API_V1},
    {"platform.clock", RISC_PLATFORM_CLOCK_API_V1},
    {RISC_BQ25896_PROFILE_CAPABILITY, RISC_BQ25896_PROFILE_API_V1}
  };
  RuntimeProviders::GraphV2 graph;
  // Registration order does not determine dependency resolution or trust.
  assert(graph.addVerified({"board-power-t5s3-v2", argv[3],
                            "board.power.vbus", RISC_USB_VBUS_API_V1,
                            needs, 3}));
  assert(graph.addVerified({"platform-clock-v1", argv[2],
                            "platform.clock", RISC_PLATFORM_CLOCK_API_V1,
                            nullptr, 0}));
  assert(graph.addVerified({"fixture-i2c", argv[1], "i2c.bus",
                            RISC_I2C_BUS_API_V1, nullptr, 0}));
  assert(graph.addVerified({"t5s3-usb-power-profile", argv[4],
                            RISC_BQ25896_PROFILE_CAPABILITY, 1, nullptr, 0}));
  auto power_grant = graph.acquire("board.power.vbus", RISC_USB_VBUS_API_V1);
  assert(power_grant.slot);
  auto *power = static_cast<const risc_usb_vbus_api_v1 *>(graph.interfaceFor(power_grant));
  assert(power && power->api_version == RISC_USB_VBUS_API_V1);
  uint64_t source = 0;
  assert(power->acquire_host(power->context, 500u, &source) && source);
  assert(!power->quiesce(power->context));
  assert(!graph.shutdown()); // live grant and source cannot be silently revoked
  assert(!power->release_host(power->context, source + 1u));
  assert(power->release_host(power->context, source));
  assert(power->quiesce(power->context)); // releases the last real lower claim
  assert(graph.release(power_grant));
  assert(!graph.interfaceFor(power_grant));
  assert(graph.shutdown()); // clock and mock bus unload after last dependent

  RuntimeProviders::GraphV2 without_clock;
  assert(without_clock.addVerified({"fixture-i2c", argv[1], "i2c.bus", 1,
                                    nullptr, 0}));
  assert(without_clock.addVerified({"board-power-t5s3-v2", argv[3],
                                   "board.power.vbus", 1, needs, 3}));
  assert(without_clock.addVerified({"t5s3-usb-power-profile", argv[4],
                                    RISC_BQ25896_PROFILE_CAPABILITY, 1, nullptr, 0}));
  assert(!without_clock.acquire("board.power.vbus", 1).slot);
  assert(without_clock.shutdown());

  RuntimeProviders::GraphV2 without_bus;
  assert(without_bus.addVerified({"platform-clock-v1", argv[2],
                                  "platform.clock", 1, nullptr, 0}));
  assert(without_bus.addVerified({"board-power-t5s3-v2", argv[3],
                                  "board.power.vbus", 1, needs, 3}));
  assert(without_bus.addVerified({"t5s3-usb-power-profile", argv[4],
                                  RISC_BQ25896_PROFILE_CAPABILITY, 1, nullptr, 0}));
  assert(!without_bus.acquire("board.power.vbus", 1).slot);
  assert(without_bus.shutdown());
  RuntimeProviders::GraphV2 without_profile;
  assert(without_profile.addVerified({"platform-clock-v1", argv[2], "platform.clock", 1, nullptr, 0}));
  assert(without_profile.addVerified({"fixture-i2c", argv[1], "i2c.bus", 1, nullptr, 0}));
  assert(without_profile.addVerified({"board-power-t5s3-v2", argv[3], "board.power.vbus", 1, needs, 3}));
  assert(!without_profile.acquire("board.power.vbus", 1).slot);
  assert(without_profile.shutdown());
  std::puts("Four ELF chain: clock + test I2C + BQ25896 + profile, dependency lifecycle PASS");
}
