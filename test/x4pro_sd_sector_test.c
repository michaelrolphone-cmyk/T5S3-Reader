#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include <stdio.h>
#include <string.h>

uint8_t x4_card_sector[512];
uint8_t x4_card_partition_boot[512];
uint8_t x4_card_fat_sector[512], x4_card_root_sector[512];
uint8_t x4_card_folder_sector[512];
uint8_t x4_card_file_sector[2][512];
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
    failures += expect(volume->ready(0), "CRC-verified FAT32 root mounted read-only");
    failures += expect(x4_card_cmd17_count == 3u, "boot, FAT and root sectors read");
    risc_storage_dirent_v1 entry;
    failures += expect(volume->dir_open(0, "/") == 1u, "root directory opens");
    failures += expect(!volume->dir_next(0, 1u, &entry), "empty root ends");
    volume->dir_close(0, 1u);
    memcpy(x4_card_root_sector, "BOOK    TXT", 11);
    x4_card_root_sector[11] = 0x20u;
    put16(x4_card_root_sector + 26, 3u);
    put32(x4_card_root_sector + 28, 600u);
    put32(x4_card_fat_sector + 12, 4u);
    put32(x4_card_fat_sector + 16, 0x0fffffffu);
    memset(x4_card_file_sector[0], 'A', 512);
    memset(x4_card_file_sector[1], 'B', 512);
    const unsigned before_directory = x4_card_cmd17_count;
    failures += expect(volume->dir_open(0, "/") == 1u, "populated root opens");
    failures += expect(volume->dir_next(0, 1u, &entry) &&
                       strcmp(entry.name, "BOOK.TXT") == 0 && entry.size == 600u,
                       "short root entry through provider API");
    failures += expect(!volume->dir_next(0, 1u, &entry) &&
                       x4_card_cmd17_count == before_directory + 1u,
                       "directory sector read once across entries and EOF");
    volume->dir_close(0, 1u);
    uint64_t stat_size = 0;
    bool is_directory = true;
    failures += expect(volume->stat(0, "/BOOK.TXT", &stat_size, &is_directory) &&
                       stat_size == 600u && !is_directory, "file stat through provider API");
    uint64_t file_size = 0;
    failures += expect(volume->file_open_read(0, "/BOOK.TXT", &file_size) == 1u &&
                       file_size == 600u, "root file opens through provider API");
    uint8_t file_data[512] = {0};
    failures += expect(volume->file_read(0, 1u, file_data, sizeof(file_data)) == 512u &&
                       file_data[0] == 'A' && file_data[511] == 'A', "first cluster read");
    failures += expect(volume->file_read(0, 1u, file_data, sizeof(file_data)) == 88u &&
                       file_data[0] == 'B' && file_data[87] == 'B', "FAT chain tail read");
    failures += expect(volume->file_read(0, 1u, file_data, sizeof(file_data)) == 0u,
                       "EOF bounded");
    failures += expect(volume->file_close(0, 1u, false), "read handle closes");
    for (unsigned i = 0; i < 512u; ++i) x4_card_file_sector[0][i] = (uint8_t)i;
    failures += expect(volume->file_open_read(0, "/BOOK.TXT", &file_size) == 1u,
                       "file reopens for partial reads");
    const unsigned before_chunks = x4_card_cmd17_count;
    failures += expect(volume->file_read(0, 1u, file_data, 48u) == 48u &&
                       file_data[0] == 0u && file_data[47] == 47u &&
                       volume->file_read(0, 1u, file_data, 48u) == 48u &&
                       file_data[0] == 48u && file_data[47] == 95u,
                       "successive 48-byte Reader chunks keep position");
    failures += expect(x4_card_cmd17_count == before_chunks + 1u,
                       "partial Reader chunks share one CRC-verified data sector");
    failures += expect(volume->file_close(0, 1u, true),
                       "read handle closes regardless of write commit flag");
    uint8_t saved_root[512];
    memcpy(saved_root, x4_card_root_sector, sizeof(saved_root));
    memset(x4_card_root_sector, 0, sizeof(x4_card_root_sector));
    memcpy(x4_card_root_sector, "BOOKS      ", 11);
    x4_card_root_sector[11] = 0x10u;
    put16(x4_card_root_sector + 26, 5u);
    memcpy(x4_card_folder_sector, saved_root, sizeof(saved_root));
    put32(x4_card_fat_sector + 20, 0x0fffffffu);
    failures += expect(volume->stat(0, "/BOOKS", &stat_size, &is_directory) &&
                       is_directory, "short-name child directory stat");
    failures += expect(volume->dir_open(0, "/BOOKS") == 1u &&
                       volume->dir_next(0, 1u, &entry) &&
                       strcmp(entry.name, "BOOK.TXT") == 0,
                       "short-name child directory lists file");
    volume->dir_close(0, 1u);
    failures += expect(volume->file_open_read(0, "/BOOKS/BOOK.TXT", &file_size) == 1u &&
                       volume->file_read(0, 1u, file_data, 48u) == 48u,
                       "nested short-name file reads through volume");
    failures += expect(volume->file_close(0, 1u, false), "nested read closes");
    failures += expect(volume->file_open_read(0, "/Books/book.txt", &file_size) == 1u,
                       "short-name lookup is ASCII case insensitive");
    failures += expect(volume->file_close(0, 1u, false), "case-insensitive read closes");
    failures += expect(!volume->stat(0, "/BOOKS/../BOOK.TXT", &stat_size, &is_directory) &&
                       volume->file_open_read(0, "/BOOKS/BOOK.TXT/OTHER", &file_size) ==
                           RISC_STORAGE_FILE_INVALID,
                       "traversal and file-as-directory paths rejected");
    memcpy(x4_card_root_sector, saved_root, sizeof(saved_root));
    memset(x4_card_file_sector[0], 'A', 512);
    put16(x4_card_root_sector + 26, 1u);
    failures += expect(volume->file_open_read(0, "/BOOK.TXT", &file_size) ==
                       RISC_STORAGE_FILE_INVALID, "invalid first file cluster rejected");
    put16(x4_card_root_sector + 26, 3u);
    put32(x4_card_fat_sector + 12, 3u);
    failures += expect(volume->file_open_read(0, "/BOOK.TXT", &file_size) == 1u &&
                       volume->file_read(0, 1u, file_data, sizeof(file_data)) == 512u &&
                       volume->file_read(0, 1u, file_data, sizeof(file_data)) == 0u &&
                       !volume->ready(0), "self-linked file chain closes mount");
    put32(x4_card_fat_sector + 12, 4u);
    failures += expect(volume->refresh(0) && volume->ready(0), "chain repair remounts");
    failures += expect(volume->file_open_write(0, "/NEW.TXT") == RISC_STORAGE_FILE_INVALID &&
                       !volume->remove(0, "/BOOK.TXT"), "write and remove fail closed");
    failures += expect(volume->file_open_read(0, "/BOOK.TXT", &file_size) == 1u,
                       "file reopens for media error test");
    x4_card_bad_crc_lba = 2081u;
    failures += expect(volume->file_read(0, 1u, file_data, sizeof(file_data)) == 0u &&
                       !volume->ready(0), "file data CRC failure invalidates mount");
    x4_card_bad_crc_lba = UINT32_MAX;
    failures += expect(volume->refresh(0) && volume->ready(0), "healthy card remounts");
    failures += expect(volume->file_read(0, 1u, file_data, sizeof(file_data)) == 0u,
                       "refresh invalidates stale file handle");
    x4_card_root_sector[0] = 0u;

    x4_card_bad_crc_lba = 32u;
    failures += expect(volume->refresh(0), "bad FAT CRC refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 table read failed") == 0,
                       "bad FAT CRC closes mount");
    x4_card_bad_crc_lba = 2080u;
    failures += expect(volume->refresh(0), "bad root CRC refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 data read failed") == 0,
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
                       strcmp(error, "FAT32 cluster chain invalid") == 0,
                       "bad FAT root chain closes mount");
    put32(x4_card_fat_sector + 8, 0x0fffffffu);
    put32(x4_card_fat_sector + 8, 2u);
    failures += expect(volume->refresh(0), "self-linked root refresh serviced");
    failures += expect(volume->last_error(0, error, sizeof(error)) &&
                       strcmp(error, "FAT32 cluster chain invalid") == 0,
                       "self-linked root rejected");
    put32(x4_card_fat_sector + 8, 0x0fffffffu);
    memset(x4_card_sector, 0, sizeof(x4_card_sector));
    x4_card_sector[446 + 4] = 0x0cu;
    put32(x4_card_sector + 446 + 8, 1);
    put32(x4_card_sector + 446 + 12, 131072);
    x4_card_sector[510] = 0x55u; x4_card_sector[511] = 0xaau;
    fat32_boot(x4_card_partition_boot);
    failures += expect(volume->refresh(0), "MBR FAT32 refresh serviced");
    failures += expect(volume->ready(0), "MBR FAT and root mounted read-only");
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
    failures += expect(volume->ready(0), "healthy retry restores read-only mount");
    failures += expect(!x4_card_bad_pin && x4_card_clock_count < 450000u,
                       "bounded traffic on assigned pins");
    driver->stop();
    if (failures) return 1;
    puts("x4 SD sector read, failure and retry: PASS");
    return 0;
}
