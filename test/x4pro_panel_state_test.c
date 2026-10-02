#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_pins.h"
#include "x4pro_mmio.h"
#include <stdio.h>
uint8_t x4_fake_busy;
uint32_t x4_fake_refresh_count;
uint32_t x4_fake_bytes;
static uint64_t fake_now;
static uint64_t fake_monotonic(void *context) { (void)context; return fake_now; }
static void fake_sleep(void *context, uint32_t ms) {
    (void)context;
    fake_now += ms;
    x4_fake_sleep_hook();
}
static const risc_platform_clock_api_v1 clock = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), 0, fake_monotonic, fake_sleep
};
static int failures;
static void expect(int cond, const char *message) {
    if (!cond) { fprintf(stderr, "FAIL %s\n", message); ++failures; }
}
int main(void) {
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    risc_provider_dependency_v1 dep = {"platform.clock", 1, &clock};
    x4_fake_busy = 1;
    expect(driver->start(&dep, 1), "start");
    const risc_display_output_api_v1 *api = driver->capability;
    risc_display_surface_v1 surface = {0};
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire");
    risc_display_present_token_v1 token = 0;
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit");
    expect(!api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "locked while queued");
    risc_display_present_status_v1 status = {0};
    expect(api->wait_present(api->context, token, 0, &status), "zero poll");
    expect(status.state == RISC_DISPLAY_PRESENT_QUEUED, "zero does not start");
    uint32_t refreshes = x4_fake_refresh_count;
    expect(api->wait_present(api->context, token, 40, &status), "success wait");
    expect(status.state == RISC_DISPLAY_PRESENT_COMPLETE, "complete");
    expect(api->wait_present(api->context, token, 40, &status), "repeat");
    expect(status.state == RISC_DISPLAY_PRESENT_COMPLETE && x4_fake_refresh_count == refreshes + 1, "no retransmit");
    x4_fake_busy = 0;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "reacquire");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit missing busy");
    expect(api->wait_present(api->context, token, 10, &status), "missing busy wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED, "never asserted");
    expect(api->wait_present(api->context, token, 10, &status) && status.state == RISC_DISPLAY_PRESENT_FAILED, "failed stays failed");
    x4_fake_busy = 2;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire stuck");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit stuck");
    expect(api->wait_present(api->context, token, 5, &status), "stuck wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED, "stuck busy fails");
    if (failures) return 1;
    puts("x4 panel states: PASS");
    return 0;
}
