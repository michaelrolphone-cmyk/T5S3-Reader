/* Reuse the actual I2C provider's existing pin/clock/CPU-ABI fixture. Both
 * physical protocol implementations below are production C, not API fakes. */
/* Renaming C main removes its implicit return-0 rule; this entry is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main existing_i2c_fixture_main
#define t5_driver_get x4_i2c_get
#include "../drivers/x4pro_i2c_test.c"
#undef t5_driver_get
#undef main
#pragma GCC diagnostic pop
#include "RiscRtcClockV2.h"
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static void append_transfer(unsigned write_len, const uint8_t *bytes, unsigned length) {
    push(true);
    for (unsigned i = 0; i < 1u + write_len + (length ? 1u : 0u); ++i) push(false);
    for (unsigned i = 0; i < length; ++i)
        for (int bit = 7; bit >= 0; --bit) push((bytes[i] >> bit) & 1u);
    push(true);
}
int main(void) {
    driver = x4_i2c_get(2); bus = driver->capability;
    diagnostics = (const risc_driver_diagnostics_v2 *)driver;
    reset_model(); start_bus();
    const risc_driver_v2 *rtc = t5_driver_get(2);
    const risc_rtc_clock_api_v2 *api = rtc->capability;
    const risc_provider_dependency_v1 dependency = {"i2c.bus", 1, bus};
    uint8_t zero = 0, bytes[] = {0x56,0x34,0x12,0x04,0,0x10,0x26};
    prepare_read(1, &zero, 1);
    assert(rtc->start(&dependency, 1));
    assert(starts == 2 && stops == 1 && data_index == data_count);
    uint64_t duplicate = 99;
    assert(!bus->claim_device(0, 0x51, &duplicate) && !duplicate);
    assert(bus->claim_device(0, 0x5d, &touch_claim));
    assert(bus->claim_device(0, 0x63, &battery_claim));
    reset_model(); append_transfer(1, &zero, 1); append_transfer(1, bytes, sizeof(bytes));
    risc_rtc_time_v2 value = {0};
    assert(api->read(0, &value) && value.year == 2026 && value.hour == 12);
    assert(starts == 4 && stops == 2 && data_index == data_count);
    reset_model(); bytes[0] |= 0x80;
    append_transfer(1, &zero, 1); append_transfer(1, bytes, sizeof(bytes));
    value.year = 1234; assert(!api->read(0, &value) && value.year == 1234);
    assert(data_index == data_count);
    value = (risc_rtc_time_v2){2026,10,4,0,12,34,56};
    reset_model(); append_transfer(1, &zero, 1); append_transfer(8, 0, 0);
    assert(api->write(0, &value) && data_index == data_count && stops == 2);
    const unsigned untouched = io_count;
    assert(!api->alarm(0,1,255,255,255,true) && io_count == untouched);
    // Real bus refused task context: failed RTC read does not change output.
    reset_model(); isr_context = true;
    assert(!api->read(0, &value)); isr_context = false; assert(io_count == 0);
    // Failed STOP poisons the real bus. RTC must retain its exact chip claim.
    reset_model(); push(true); push(true); push(false);
    assert(!api->read(0, &value));
    push(false); assert(!rtc->quiesce());
    assert(!rtc->start(&dependency, 1));
    push(true); assert(rtc->quiesce());
    assert(bus->release_device(0, touch_claim));
    assert(bus->release_device(0, battery_claim));
    assert(driver->quiesce()); driver->stop();
    assert(creates == deletes);
    puts("RTC + actual X4 I2C: shared claims, valid/VL reads, write, ISR refusal and retained STOP PASS");
}
