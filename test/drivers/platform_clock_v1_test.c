#define _DEFAULT_SOURCE 1
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static int fail_clock, invalid_nsec, calls, sleeps[8], sleep_count;
static time_t seconds = 2;

int clock_gettime(clockid_t id, struct timespec *ts) {
    assert(id == CLOCK_MONOTONIC);
    ++calls;
    if (fail_clock) return -1;
    ts->tv_sec = seconds;
    ts->tv_nsec = invalid_nsec ? 1000000000L : 123456789L;
    return 0;
}
int usleep(useconds_t micros) {
    assert(sleep_count < 8);
    sleeps[sleep_count++] = (int)micros;
    return 0;
}

int main(void) {
    assert(t5_driver_get(1u) == NULL);
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(*driver));
    assert(driver->capability_api == RISC_PLATFORM_CLOCK_API_V1);
    assert(driver->quiesce && driver->stop && driver->start);
    const risc_platform_clock_api_v1 *api = driver->capability;
    assert(api && api->api_version == RISC_PLATFORM_CLOCK_API_V1);
    assert(api->struct_size == sizeof(*api) && api->monotonic_ms && api->sleep_ms);
    assert(api->monotonic_ms(api->context) == UINT64_MAX); /* not started */
    api->sleep_ms(api->context, 10u);
    assert(sleep_count == 0);
    assert(!driver->start(NULL, 1u));
    fail_clock = 1;
    assert(!driver->start(NULL, 0));
    fail_clock = 0;
    assert(driver->start(NULL, 0));
    assert(!driver->start(NULL, 0));
    assert(api->monotonic_ms(api->context) == 2123u);
    fail_clock = 1;
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    fail_clock = 0;
    invalid_nsec = 1;
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    invalid_nsec = 0;
    api->sleep_ms(api->context, 2305u);
    assert(sleep_count == 3 && sleeps[0] == 999000 &&
           sleeps[1] == 999000 && sleeps[2] == 307000);
    assert(driver->quiesce());
    driver->stop();
    assert(api->monotonic_ms(api->context) == UINT64_MAX);
    api->sleep_ms(api->context, 99u);
    assert(sleep_count == 3);
    seconds = -1;
    assert(!driver->start(NULL, 0));
    puts("Generic platform.clock ELF: monotonic source, failure sentinel, bounded OS sleeps, lifecycle PASS");
    return 0;
}
