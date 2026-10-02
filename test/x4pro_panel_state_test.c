#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_pins.h"
#include "x4pro_mmio.h"
#include <stdio.h>
#include <string.h>
uint8_t x4_fake_busy;
uint32_t x4_fake_refresh_count;
uint32_t x4_fake_bytes;
uint32_t x4_fake_cmd46;
uint32_t x4_fake_cmd47;
uint32_t x4_fake_cmd24;
uint32_t x4_fake_cmd26;
uint8_t x4_fake_hold_busy;
uint8_t x4_fake_uc_stage;
uint32_t x4_fake_uc_cmd13;
uint32_t x4_fake_uc_cmd10;
uint32_t x4_fake_uc_pon_early;
uint32_t x4_fake_output_mask;
uint32_t x4_fake_level_before_config;
int x4_test_probe_mode = 4;
static uint64_t fake_now;
static uint32_t charge_ms;
static uint32_t charge_every;
static uint32_t level_count;
static int fail_clock;
void x4_fake_on_level(void) {
    ++level_count;
    if (charge_ms && (!charge_every || level_count % charge_every == 0)) fake_now += charge_ms;
}
static uint64_t fake_monotonic(void *context) {
    (void)context;
    if (fail_clock == 1) return UINT64_MAX;
    if (fail_clock == 2) { fake_now -= 1; return fake_now; }
    return fake_now;
}
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
    x4_fake_busy = 0;
    expect(driver->start(&dep, 1), "start");
    expect((x4_fake_output_mask & ((1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 18))) == ((1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 18)), "spi outputs configured");
    expect((x4_fake_level_before_config & ((1u << 11) | (1u << 12) | (1u << 13) | (1u << 18))) == 0, "no level before config");
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
    x4_fake_busy = 1;
    expect(api->wait_present(api->context, token, 40, &status), "success wait");
    expect(status.state == RISC_DISPLAY_PRESENT_COMPLETE, "complete");
    expect(x4_fake_cmd24 == 1 && x4_fake_cmd26 == 1, "full refresh writes both RAM planes");
    expect(api->wait_present(api->context, token, 40, &status), "repeat");
    expect(status.state == RISC_DISPLAY_PRESENT_COMPLETE && x4_fake_refresh_count == refreshes + 1, "no retransmit");
    x4_fake_busy = 0;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "reacquire");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit missing busy");
    expect(api->wait_present(api->context, token, 10, &status), "missing busy wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED, "never asserted");
    expect(api->wait_present(api->context, token, 10, &status) && status.state == RISC_DISPLAY_PRESENT_FAILED, "failed stays failed");
    x4_fake_busy = 3;
    x4_fake_refresh_count = 0;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire stuck");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit stuck");
    expect(api->wait_present(api->context, token, 5, &status), "stuck wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED, "stuck busy fails");
    expect(!driver->quiesce(), "busy panel remains pinned after timeout");
    char detail[160];
    const risc_driver_diagnostics_v2 *diag = (const risc_driver_diagnostics_v2 *)driver;
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "busy completion timeout"), "stuck reason");
    charge_ms = 1;
    x4_fake_busy = 0;
    x4_fake_hold_busy = 0;
    uint32_t before = x4_fake_refresh_count;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire transfer deadline");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit transfer deadline");
    expect(api->wait_present(api->context, token, 5, &status), "transfer deadline wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED, "transfer deadline state");
    expect(x4_fake_refresh_count == before, "no refresh after transfer deadline");
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "transfer deadline"), "transfer reason");
    expect(api->wait_present(api->context, token, 5, &status) && status.state == RISC_DISPLAY_PRESENT_FAILED, "deadline stays failed");
    expect(x4_fake_refresh_count == before, "deadline does not retransmit");
    charge_every = 200000;
    level_count = 0;
    before = x4_fake_refresh_count;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire final chunk");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit final chunk");
    expect(api->wait_present(api->context, token, 4, &status), "final chunk wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED && x4_fake_refresh_count == before, "final chunk does not refresh");
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "transfer deadline"), "final chunk reason");
    charge_ms = 0;
    charge_every = 0;
    fail_clock = 1;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire clock");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit clock");
    expect(api->wait_present(api->context, token, 20, &status), "clock wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED && diag->last_error(detail, sizeof(detail)) && strstr(detail, "clock failure"), "clock reason");
    fail_clock = 2;
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "acquire nonmonotonic");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "submit nonmonotonic");
    expect(api->wait_present(api->context, token, 20, &status), "nonmonotonic wait");
    expect(status.state == RISC_DISPLAY_PRESENT_FAILED && diag->last_error(detail, sizeof(detail)) && strstr(detail, "clock nonmonotonic"), "nonmonotonic reason");
    expect(x4_fake_cmd46 == 1 && x4_fake_cmd47 == 1, "ram clears waited in order");
    x4_test_probe_mode = 4;
    x4_fake_busy = 5;
    x4_fake_cmd46 = x4_fake_cmd47 = x4_fake_cmd24 = 0;
    expect(!driver->start(&dep, 1), "stuck ram init");
    expect(x4_fake_cmd46 == 1 && x4_fake_cmd47 == 0 && x4_fake_cmd24 == 0, "stuck init does not continue");
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "first-ram busy timeout"), "first ram reason");
    x4_test_probe_mode = 1;
    expect(!driver->start(&dep, 1), "probe disabled");
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "probe-disabled"), "disabled reason");
    x4_test_probe_mode = 3;
    fail_clock = 0;
    charge_ms = 0;
    charge_every = 0;
    x4_fake_busy = 6;
    expect(driver->start(&dep, 1), "uc8279 start");
    expect(api->acquire(api->context, RISC_DISPLAY_FORMAT_MONO1, &surface), "uc acquire");
    expect(api->submit(api->context, surface.frame, 0, 0, 0, &token), "uc submit");
    expect(api->wait_present(api->context, token, 20000, &status), "uc wait");
    expect(status.state == RISC_DISPLAY_PRESENT_COMPLETE, "uc complete");
    expect(x4_fake_uc_cmd13 == 1 && x4_fake_uc_cmd10 == 1, "uc new and old planes");
    expect(x4_fake_uc_pon_early == 0, "uc PON settled before PSR");
    expect(driver->quiesce(), "uc idle can quiesce");
    x4_test_probe_mode = 2;
    expect(!driver->start(&dep, 1), "ambiguous rejected");
    expect(diag->last_error(detail, sizeof(detail)) && strstr(detail, "ambiguous-controller"), "ambiguous reason");
    x4_fake_busy = 2;
    x4_fake_hold_busy = 0;
    expect(driver->quiesce(), "idle panel can quiesce");
    if (failures) return 1;
    puts("x4 panel states: PASS");
    return 0;
}
