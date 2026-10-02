#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include <stdio.h>
#include <string.h>

uint8_t x4_card_sector[512];
uint8_t x4_card_partition_boot[512];
uint8_t x4_card_fat_sector[512], x4_card_root_sector[512];
bool x4_card_bad_crc, x4_card_no_data, x4_card_bad_pin;
uint32_t x4_card_bad_crc_lba = UINT32_MAX;
unsigned x4_card_cmd17_count, x4_card_clock_count;
static uint64_t now_ms;
static uint64_t monotonic_ms(void *context) { (void)context; return now_ms; }
static void sleep_ms(void *context, uint32_t ms) { (void)context; now_ms += ms; }
static int expect(bool condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}
static void put16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static void fat32_boot(uint8_t *sector) {
    memset(sector, 0, 512);
    put16(sector + 11, 512);
    sector[13] = 1; /* sectors per cluster */
    put16(sector + 14, 32); /* reserved sectors */
    sector[16] = 2; /* FAT copies */
    put32(sector + 32, 131072); /* total sectors */
    put32(sector + 36, 1024); /* FAT sectors */
    put32(sector + 44, 2); /* root cluster */
    sector[510] = 0x55u; sector[511] = 0xaau;
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
    fat32_boot(x4_card_sector);
    put32(x4_card_fat_sector + 8, 0x0fffffffu); /* root cluster 2 ends here */
    x4_card_root_sector[0] = 0u; /* empty root directory */
    failures += expect(driver->start(&dependency, 1), "provider start");
    char error[80] = {0};
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root read; filesystem not mounted") == 0,
                       "CRC-verified FAT32 root remains unmounted");
    failures += expect(!volume->ready(0), "readable sector is not a filesystem");
    failures += expect(x4_card_cmd17_count == 3u, "boot, FAT and root sectors read");

    x4_card_bad_crc_lba = 32u;
    failures += expect(volume->refresh(0), "bad FAT CRC refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 table read failed") == 0,
                       "bad FAT CRC closes mount");
    x4_card_bad_crc_lba = 2080u;
    failures += expect(volume->refresh(0), "bad root CRC refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root read failed") == 0,
                       "bad root CRC closes mount");
    x4_card_bad_crc_lba = UINT32_MAX;

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
    x4_card_sector[13] = 3u;
    failures += expect(volume->refresh(0), "invalid cluster size refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 volume absent") == 0,
                       "invalid FAT32 layout closes mount");
    x4_card_sector[13] = 1u;
    put32(x4_card_fat_sector + 8, 1u);
    failures += expect(volume->refresh(0), "bad root chain refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root chain invalid") == 0,
                       "bad FAT root chain closes mount");
    put32(x4_card_fat_sector + 8, 0x0fffffffu);
    put32(x4_card_fat_sector + 8, 2u);
    failures += expect(volume->refresh(0), "self-linked root refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root chain invalid") == 0,
                       "self-linked root rejected");
    put32(x4_card_fat_sector + 8, 0x0fffffffu);
    memset(x4_card_sector, 0, sizeof(x4_card_sector));
    x4_card_sector[446 + 4] = 0x0cu;
    put32(x4_card_sector + 446 + 8, 1);
    put32(x4_card_sector + 446 + 12, 131072);
    x4_card_sector[510] = 0x55u; x4_card_sector[511] = 0xaau;
    fat32_boot(x4_card_partition_boot);
    failures += expect(volume->refresh(0), "MBR FAT32 refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root read; filesystem not mounted") == 0,
                       "MBR FAT and root verified without mount");
    failures += expect(x4_card_cmd17_count >= 7u, "MBR reads boot, FAT and root");
    x4_card_partition_boot[36] = 0;
    x4_card_partition_boot[37] = 0;
    failures += expect(volume->refresh(0), "invalid partition boot refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 boot invalid") == 0,
                       "invalid partition boot closes mount");
    fat32_boot(x4_card_partition_boot);
    x4_card_no_data = true;
    const uint64_t before = now_ms;
    failures += expect(volume->refresh(0), "missing data token refresh serviced");
    failures += expect(now_ms - before <= 700u, "missing data token bounded");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "sector 0 read failed") == 0, "missing token closes mount");
    x4_card_no_data = false;
    failures += expect(volume->refresh(0), "healthy retry serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 root read; filesystem not mounted") == 0,
                       "healthy retry restores root proof only");
    failures += expect(!volume->ready(0), "filesystem remains unavailable");
    failures += expect(!x4_card_bad_pin && x4_card_clock_count < 450000u,
                       "bounded traffic on assigned pins");
    driver->stop();
    if (failures) return 1;
    puts("x4 SD sector read, failure and retry: PASS");
    return 0;
}
