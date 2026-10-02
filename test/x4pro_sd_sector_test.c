#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include <stdio.h>
#include <string.h>

uint8_t x4_card_sector[512];
bool x4_card_bad_crc, x4_card_no_data, x4_card_bad_pin;
unsigned x4_card_cmd17_count, x4_card_clock_count;
static uint64_t now_ms;
static uint64_t monotonic_ms(void *context) { (void)context; return now_ms; }
static void sleep_ms(void *context, uint32_t ms) { (void)context; now_ms += ms; }
static int expect(bool condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}
int main(void) {
    int failures = 0;
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    failures += expect(driver && driver->capability, "driver ABI");
    if (failures) return 1;
    const risc_storage_volume_api_v1 *volume = driver->capability;
    const risc_platform_clock_api_v1 clock = {
        RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), 0, monotonic_ms, sleep_ms
    };
    const risc_provider_dependency_v1 dependency = {"platform.clock", 1, &clock};
    x4_card_sector[510] = 0x55u; x4_card_sector[511] = 0xaau;
    failures += expect(driver->start(&dependency, 1), "provider start");
    char error[80] = {0};
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 read; filesystem not mounted") == 0,
                       "CRC-verified sector read remains unmounted");
    failures += expect(!volume->ready(0), "readable sector is not a filesystem");
    failures += expect(x4_card_cmd17_count == 1u, "one sector read");

    x4_card_bad_crc = true;
    failures += expect(volume->refresh(0), "CRC failure refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 read failed") == 0, "CRC failure closes mount");
    failures += expect(!volume->ready(0), "CRC failure unavailable");
    x4_card_bad_crc = false;
    x4_card_sector[511] = 0;
    failures += expect(volume->refresh(0), "invalid boot signature refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 signature invalid") == 0,
                       "invalid boot signature closes mount");
    x4_card_sector[511] = 0xaau;
    x4_card_no_data = true;
    const uint64_t before = now_ms;
    failures += expect(volume->refresh(0), "missing data token refresh serviced");
    failures += expect(now_ms - before <= 700u, "missing data token bounded");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 read failed") == 0, "missing token closes mount");
    x4_card_no_data = false;
    failures += expect(volume->refresh(0), "healthy retry serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 read; filesystem not mounted") == 0,
                       "healthy retry restores block proof only");
    failures += expect(!x4_card_bad_pin && x4_card_clock_count < 180000u,
                       "bounded traffic on assigned pins");
    driver->stop();
    if (failures) return 1;
    puts("x4 SD sector read, failure and retry: PASS");
    return 0;
}
