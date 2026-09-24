#include <bq25896.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
  // Empty-host automatic role checks may source and stop repeatedly. None
  // of these observations may produce an incoming-power/UI refresh edge.
  const uint8_t empty[][3] = {
      {0x10, 0x00, 0x00}, // source off
      {0x20, 0x00, 0x00}, // boost requested, status not ready yet
      {0x20, 0xe0, 0x19}, // established source, 5.1 V ADC
      {0x20, 0xe4, 0x99}, // source overrides PG/VBUS-good
      {0x10, 0xe0, 0x19}, // shutdown status still says OTG
      {0x10, 0x00, 0x19}, // source off, old ADC voltage is not input
  };
  for (unsigned cycle = 0; cycle < 100; ++cycle)
    for (unsigned i = 0; i < sizeof(empty) / sizeof(empty[0]); ++i)
      assert(!bq25896_has_external_input(empty[i][0], empty[i][1], empty[i][2]));
  assert(bq25896_has_external_input(0x10, 0x24, 0x99)); // PC
  assert(bq25896_has_external_input(0x10, 0x44, 0x99)); // charger
  assert(bq25896_has_external_input(0x10, 0x5c, 0x99)); // battery full
  assert(bq25896_has_external_input(0x10, 0x04, 0));    // PG before type
  assert(bq25896_has_external_input(0x10, 0, 0x80));    // input detect
  assert(!bq25896_has_external_input(0x20, 0x24, 0x99)); // boost transition
  assert(!bq25896_has_external_input(0x10, 0x18, 0));   // charge bits alone
  puts("BQ25896 UI telemetry distinguishes incoming power from OTG output: PASS");
}
