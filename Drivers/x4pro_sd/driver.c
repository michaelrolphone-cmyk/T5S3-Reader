/* X4 Pro 1-bit SD. Owns GPIO 5/40/41/42. Command frames use the tested host
 * direction bit. Filesystem calls fail closed until a FAT boot sector parses. */
#include "RiscPlatformClockV1.h"
#include "RiscProviderV2.h"
#include "RiscStorageVolumeV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const risc_platform_clock_api_v1 *clock_api;
static bool started, high_capacity;
static char error[80];
static bool mounted;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(error)) { error[i] = text[i]; ++i; }
    error[i] = 0;
}
static void tick(void) {
    x4pro_pin_output(X4PRO_PIN_SD_CLK, true);
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
}
static bool wait_dat0(bool level, uint32_t max_clocks, uint32_t budget_ms) {
    const uint64_t began = clock_api->monotonic_ms(clock_api->context);
    for (uint32_t i = 0; i < max_clocks; ++i) {
        if (x4pro_pin_read(X4PRO_PIN_SD_DAT0) == level) return true;
        tick();
        if ((i & 255u) == 255u) {
            clock_api->sleep_ms(clock_api->context, 1);
            if (clock_api->monotonic_ms(clock_api->context) - began >= budget_ms) break;
        }
    }
    return false;
}
static void cmd_bit(bool bit) { x4pro_pin_output(X4PRO_PIN_SD_CMD, bit); tick(); }
static bool command(uint8_t index, uint32_t arg, uint8_t *response, size_t length) {
    if (length > 17u || (length && !response)) return false;
    uint8_t frame[6];
    x4pro_sd_command(index, arg, frame);
    for (int i = 0; i < 8; ++i) tick();
    for (size_t byte = 0; byte < sizeof(frame); ++byte)
        for (int bit = 7; bit >= 0; --bit) cmd_bit((frame[byte] >> bit) & 1);
    x4pro_pin_release(X4PRO_PIN_SD_CMD);
    /* CMD0 has no response on the native SD bus. */
    if (!length) { for (int i = 0; i < 8; ++i) tick(); return true; }
    bool seen = false;
    for (int i = 0; i < 64 && !seen; ++i) {
        seen = !x4pro_pin_read(X4PRO_PIN_SD_CMD);
        if (!seen) tick();
    }
    if (!seen) return false;
    memset(response, 0, length);
    for (size_t byte = 0; byte < length; ++byte) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            bool level = x4pro_pin_read(X4PRO_PIN_SD_CMD);
            tick();
            value = (uint8_t)((value << 1) | (level ? 1u : 0u));
        }
        response[byte] = value;
    }
    return true;
}
static bool response_for(uint8_t index, const uint8_t response[6]) {
    return (response[0] & 0xc0u) == 0u && (response[0] & 0x3fu) == index;
}
static bool read_sector(uint32_t lba, uint8_t out[512]) {
    uint8_t response[6];
    if (!out || (!high_capacity && lba > UINT32_MAX / 512u) ||
        !command(17, high_capacity ? lba : lba * 512u, response, sizeof(response)) ||
        !response_for(17, response) || !wait_dat0(false, 131072u, 500u)) return false;
    tick(); /* Consume the DAT0 start bit before the first payload bit. */
    for (size_t byte = 0; byte < 512u; ++byte) {
        uint8_t value = 0;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            value = (uint8_t)((value << 1) | (x4pro_pin_read(X4PRO_PIN_SD_DAT0) ? 1u : 0u));
            tick();
        }
        out[byte] = value;
        if ((byte & 63u) == 63u) clock_api->sleep_ms(clock_api->context, 1);
    }
    uint16_t received_crc = 0;
    for (unsigned bit = 0; bit < 16u; ++bit) {
        received_crc = (uint16_t)((received_crc << 1) |
                                  (x4pro_pin_read(X4PRO_PIN_SD_DAT0) ? 1u : 0u));
        tick();
    }
    const bool stop = x4pro_pin_read(X4PRO_PIN_SD_DAT0);
    tick();
    return stop && received_crc == x4pro_sd_crc16(out, 512u);
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
typedef struct {
    uint32_t fat_lba, fat_sectors, data_lba, clusters, root_cluster;
    uint8_t sectors_per_cluster;
} fat32_geometry;
static fat32_geometry volume_geometry;
static struct {
    bool open;
    uint32_t cluster, sector, slot, traversed;
} directory_cursor;
static uint32_t directory_last_cluster;
static struct {
    bool open;
    uint32_t cluster, sector, offset, remaining, traversed;
} file_cursor;
/* Parse only layout metadata. No mounted state or file access is granted here. */
static bool fat32_layout(const uint8_t sector[512], uint32_t partition_lba,
                         uint32_t partition_sectors, fat32_geometry *out) {
    if (sector[510] != 0x55u || sector[511] != 0xaau ||
        le16(sector + 11) != 512u || sector[16] < 1u || sector[16] > 2u ||
        le16(sector + 17) != 0u || le16(sector + 19) != 0u ||
        le16(sector + 22) != 0u) return false;
    const uint32_t spc = sector[13];
    const uint32_t reserved = le16(sector + 14);
    const uint32_t fat_size = le32(sector + 36);
    const uint32_t total = le32(sector + 32);
    const uint64_t data_begin = (uint64_t)reserved + (uint64_t)sector[16] * fat_size;
    if (!out || !spc || spc > 128u || (spc & (spc - 1u)) || !reserved || !fat_size ||
        total < 65525u || (partition_sectors && total > partition_sectors) ||
        data_begin >= total || (total - data_begin) / spc < 65525u ||
        (uint64_t)partition_lba + total > UINT32_MAX) return false;
    const uint32_t clusters = (uint32_t)((total - data_begin) / spc);
    const uint32_t root = le32(sector + 44);
    /* The FAT must have one 32-bit entry per possible data cluster. */
    if (root < 2u || (uint64_t)root >= (uint64_t)clusters + 2u ||
        (uint64_t)fat_size * 128u < (uint64_t)clusters + 2u) return false;
    out->fat_lba = partition_lba + reserved;
    out->fat_sectors = fat_size;
    out->data_lba = partition_lba + (uint32_t)data_begin;
    out->clusters = clusters;
    out->root_cluster = root;
    out->sectors_per_cluster = (uint8_t)spc;
    return true;
}
/* One CRC-checked FAT lookup. A caller can advance a directory/file cursor
 * without scanning or retaining the FAT; never follow a free/bad/self link. */
static bool fat_next_cluster(const fat32_geometry *geometry, uint32_t cluster,
                             uint32_t *next_out) {
    if (!geometry || !next_out || cluster < 2u ||
        (uint64_t)cluster >= (uint64_t)geometry->clusters + 2u ||
        cluster / 128u >= geometry->fat_sectors) {
        fail("FAT32 cluster bounds invalid"); return false;
    }
    uint8_t sector[512];
    if (!read_sector(geometry->fat_lba + cluster / 128u, sector)) {
        fail("FAT32 table read failed"); mounted = false; return false;
    }
    const uint32_t next = le32(sector + (cluster % 128u) * 4u) & 0x0fffffffu;
    if (next < 0x0ffffff8u && (next < 2u || next == cluster ||
        (uint64_t)next >= (uint64_t)geometry->clusters + 2u)) {
        fail("FAT32 root chain invalid"); return false;
    }
    *next_out = next;
    return true;
}
static bool read_cluster_sector(const fat32_geometry *geometry, uint32_t cluster,
                                uint32_t offset, uint8_t out[512]) {
    if (!geometry || !out || cluster < 2u ||
        (uint64_t)cluster >= (uint64_t)geometry->clusters + 2u ||
        offset >= geometry->sectors_per_cluster) {
        fail("FAT32 data bounds invalid"); return false;
    }
    const uint32_t lba = geometry->data_lba +
        (cluster - 2u) * geometry->sectors_per_cluster + offset;
    if (!read_sector(lba, out)) {
        fail("FAT32 data read failed");
        mounted = false;
        return false;
    }
    return true;
}
static bool probe_root(const fat32_geometry *geometry) {
    uint8_t sector[512];
    uint32_t next = 0;
    if (!fat_next_cluster(geometry, geometry->root_cluster, &next) ||
        !read_cluster_sector(geometry, geometry->root_cluster, 0u, sector)) return false;
    volume_geometry = *geometry;
    mounted = true;
    return true;
}
static bool probe_fat32(void) {
    uint8_t sector[512];
    fat32_geometry geometry;
    if (!read_sector(0, sector)) { fail("sector 0 read failed"); return false; }
    if (sector[510] != 0x55u || sector[511] != 0xaau) {
        fail("sector 0 signature invalid"); return false;
    }
    if (fat32_layout(sector, 0, 0, &geometry)) return probe_root(&geometry);
    /* Prefer one ordinary FAT32 MBR partition; never guess a non-FAT volume. */
    uint32_t first = 0, count = 0;
    for (unsigned i = 0; i < 4u; ++i) {
        const uint8_t *entry = sector + 446u + 16u * i;
        const uint8_t type = entry[4];
        if (type != 0x0bu && type != 0x0cu && type != 0x1bu && type != 0x1cu) continue;
        if (first) { fail("multiple FAT32 partitions unsupported"); return false; }
        first = le32(entry + 8);
        count = le32(entry + 12);
        if (!first || !count || first > UINT32_MAX - count) {
            fail("FAT32 partition bounds invalid"); return false;
        }
    }
    if (!first) { fail("FAT32 volume absent"); return false; }
    if (!read_sector(first, sector)) { fail("FAT32 boot read failed"); return false; }
    if (!fat32_layout(sector, first, count, &geometry)) {
        fail("FAT32 boot invalid"); return false;
    }
    return probe_root(&geometry);
}
static bool init_card(void) {
    uint8_t response[17] = {0};
    high_capacity = false;
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 80);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, false);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 120);
    x4pro_pin_release(X4PRO_PIN_SD_CMD);
    x4pro_pin_release(X4PRO_PIN_SD_DAT0);
    for (int i = 0; i < 80; ++i) tick();
    if (!command(0, 0, response, 0)) { fail("CMD0 send failed"); return false; }
    if (!command(8, 0x1AAu, response, 6)) { fail("CMD8 no response"); return false; }
    if ((response[0] & 0x3fu) != 8u || response[3] != 1u || response[4] != 0xaau) {
        fail("CMD8 response invalid"); return false;
    }
    for (int i = 0; i < 200; ++i) {
        if (!command(55, 0, response, 6) || !command(41, 0x40100000u, response, 6)) {
            fail("ACMD41 failed"); return false;
        }
        if (response[1] & 0x80u) {
            high_capacity = (response[1] & 0x40u) != 0u;
            if (!command(2, 0, response, 17)) { fail("CMD2 failed"); return false; }
            if (!command(3, 0, response, 6) || !response_for(3, response)) {
                fail("CMD3 failed"); return false;
            }
            const uint32_t rca = ((uint32_t)response[1] << 24) | ((uint32_t)response[2] << 16);
            if (!rca || !command(7, rca, response, 6) || !response_for(7, response) ||
                !wait_dat0(true, 131072u, 500u)) { fail("CMD7 select failed"); return false; }
            if (!high_capacity && (!command(16, 512u, response, 6) ||
                                   !response_for(16, response))) {
                fail("CMD16 block size failed"); return false;
            }
            return probe_fat32();
        }
        if (clock_api) clock_api->sleep_ms(clock_api->context, 10);
    }
    fail("card idle");
    return false;
}
static bool refresh(void *context) {
    (void)context;
    error[0] = 0;
    mounted = false;
    directory_cursor.open = false;
    file_cursor.open = false;
    if (!started) return false;
    (void)init_card();
    return true;
}
static bool ready(void *context) { (void)context; return mounted; }
static bool label(void *context, char *out, size_t capacity) {
    (void)context;
    if (!ready(0) || !out || capacity < 6) return false;
    memcpy(out, "X4PRO", 6);
    return true;
}
static risc_storage_dir_t dir_open(void *context, const char *path);
static bool dir_next(void *context, risc_storage_dir_t directory, risc_storage_dirent_v1 *entry);
static void dir_close(void *context, risc_storage_dir_t directory);
static bool stat(void *context, const char *path, uint64_t *size_out, bool *is_directory_out) {
    (void)context;
    if (!mounted || !path || !size_out || !is_directory_out) return false;
    if (equal(path, "/")) { *size_out = 0; *is_directory_out = true; return true; }
    risc_storage_dir_t handle = dir_open(0, "/");
    if (!handle) return false;
    risc_storage_dirent_v1 entry;
    bool found = false;
    while (dir_next(0, handle, &entry)) {
        if (equal(path + (path[0] == '/' ? 1 : 0), entry.name)) {
            *size_out = entry.size;
            *is_directory_out = entry.is_directory != 0;
            found = true;
            break;
        }
    }
    dir_close(0, handle);
    return found;
}
static risc_storage_dir_t dir_open(void *context, const char *path) {
    (void)context;
    if (!mounted || !equal(path, "/")) return RISC_STORAGE_DIR_INVALID;
    directory_cursor.open = true;
    directory_cursor.cluster = volume_geometry.root_cluster;
    directory_cursor.sector = directory_cursor.slot = directory_cursor.traversed = 0;
    return 1;
}
static bool dir_next(void *context, risc_storage_dir_t directory, risc_storage_dirent_v1 *entry) {
    (void)context;
    if (!mounted || directory != 1 || !directory_cursor.open || !entry) return false;
    uint8_t sector[512];
    const uint64_t began = clock_api->monotonic_ms(clock_api->context);
    for (unsigned reads = 0; reads < 128u; ++reads) {
        if (directory_cursor.traversed >= 128u ||
            clock_api->monotonic_ms(clock_api->context) - began >= 3000u) {
            fail("FAT32 root scan limit"); return false;
        }
        if (!read_cluster_sector(&volume_geometry, directory_cursor.cluster,
                                 directory_cursor.sector, sector)) return false;
        ++directory_cursor.traversed;
        while (directory_cursor.slot < 16u) {
            const uint8_t *item = sector + 32u * directory_cursor.slot++;
            if (!item[0]) return false;
            if (item[0] == 0xe5u || item[11] == 0x0fu || (item[11] & 0x08u)) continue;
            memset(entry, 0, sizeof(*entry));
            size_t pos = 0;
            for (size_t i = 0; i < 8u && item[i] != ' '; ++i) entry->name[pos++] = (char)item[i];
            if (item[8] != ' ') {
                entry->name[pos++] = '.';
                for (size_t i = 8u; i < 11u && item[i] != ' '; ++i) entry->name[pos++] = (char)item[i];
            }
            entry->size = le32(item + 28);
            entry->is_directory = (item[11] & 0x10u) != 0u;
            directory_last_cluster = ((uint32_t)le16(item + 20) << 16) | le16(item + 26);
            return true;
        }
        directory_cursor.slot = 0;
        if (++directory_cursor.sector >= volume_geometry.sectors_per_cluster) {
            uint32_t next = 0;
            if (!fat_next_cluster(&volume_geometry, directory_cursor.cluster, &next)) return false;
            if (next >= 0x0ffffff8u) return false;
            directory_cursor.cluster = next;
            directory_cursor.sector = 0;
        }
    }
    fail("FAT32 root scan limit");
    return false;
}
static void dir_close(void *context, risc_storage_dir_t directory) {
    (void)context;
    if (directory == 1) directory_cursor.open = false;
}
static risc_storage_file_t file_open_read(void *context, const char *path, uint64_t *size_out) {
    (void)context;
    file_cursor.open = false;
    if (!mounted || !path || !size_out || !path[0]) return RISC_STORAGE_FILE_INVALID;
    const char *wanted = path + (path[0] == '/' ? 1 : 0);
    if (!wanted[0] || !dir_open(0, "/")) return RISC_STORAGE_FILE_INVALID;
    risc_storage_dirent_v1 entry;
    bool found = false;
    while (dir_next(0, 1, &entry)) {
        if (equal(wanted, entry.name) && !entry.is_directory) { found = true; break; }
    }
    dir_close(0, 1);
    if (!found) return RISC_STORAGE_FILE_INVALID;
    if (entry.size && (directory_last_cluster < 2u ||
        (uint64_t)directory_last_cluster >= (uint64_t)volume_geometry.clusters + 2u)) {
        fail("FAT32 file cluster invalid"); return RISC_STORAGE_FILE_INVALID;
    }
    file_cursor.open = true;
    file_cursor.cluster = directory_last_cluster;
    file_cursor.sector = file_cursor.offset = file_cursor.traversed = 0;
    file_cursor.remaining = (uint32_t)entry.size;
    *size_out = entry.size;
    return 1;
}
static size_t file_read(void *context, risc_storage_file_t file, void *buffer, size_t capacity) {
    (void)context;
    if (!mounted || file != 1 || !file_cursor.open || !buffer || !capacity || !file_cursor.remaining) return 0;
    if (file_cursor.sector >= volume_geometry.sectors_per_cluster) {
        uint32_t next = 0;
        if (file_cursor.traversed++ >= 1024u ||
            !fat_next_cluster(&volume_geometry, file_cursor.cluster, &next) ||
            next >= 0x0ffffff8u) { fail("FAT32 file chain truncated"); file_cursor.open = false; return 0; }
        file_cursor.cluster = next;
        file_cursor.sector = 0;
    }
    uint8_t sector[512];
    if (!read_cluster_sector(&volume_geometry, file_cursor.cluster, file_cursor.sector, sector)) {
        file_cursor.open = false; return 0;
    }
    size_t count = 512u - file_cursor.offset;
    if (count > capacity) count = capacity;
    if (count > file_cursor.remaining) count = file_cursor.remaining;
    memcpy(buffer, sector + file_cursor.offset, count);
    file_cursor.offset += (uint32_t)count;
    file_cursor.remaining -= (uint32_t)count;
    if (file_cursor.offset == 512u) { file_cursor.offset = 0; ++file_cursor.sector; }
    return count;
}
static risc_storage_file_t file_open_write(void *context, const char *path) { (void)context; (void)path; return RISC_STORAGE_FILE_INVALID; }
static size_t file_write(void *context, risc_storage_file_t file, const void *buffer, size_t size) {
    (void)context; (void)file; (void)buffer; (void)size; return 0;
}
static bool file_close(void *context, risc_storage_file_t file, bool commit) {
    (void)context;
    if (file != 1 || !file_cursor.open || commit) return false;
    file_cursor.open = false;
    return true;
}
static bool remove_path(void *context, const char *path) { (void)context; (void)path; return false; }
static bool last_error_api(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || !capacity) return false;
    size_t i = 0;
    while (error[i] && i + 1u < capacity) { out[i] = error[i]; ++i; }
    out[i] = 0;
    return error[0] != 0;
}
static const risc_storage_volume_api_v1 api = {
    RISC_STORAGE_VOLUME_API_V1, sizeof(api), 0, refresh, ready, label, stat,
    dir_open, dir_next, dir_close, file_open_read, file_read, file_open_write,
    file_write, file_close, remove_path, last_error_api
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1)
            clock_api = dependencies[i].api;
    if (!clock_api || clock_api->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        clock_api->struct_size < sizeof(*clock_api) || !clock_api->monotonic_ms ||
        !clock_api->sleep_ms) { fail("platform.clock missing or invalid"); return false; }
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    started = true;
    return refresh(0);
}
static void stop(void) {
    if (started) {
        x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
        x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
        x4pro_pin_release(X4PRO_PIN_SD_CMD);
        x4pro_pin_release(X4PRO_PIN_SD_DAT0);
    }
    started = false;
    high_capacity = false;
    mounted = false;
    directory_cursor.open = false;
    file_cursor.open = false;
}
static bool quiesce(void) { stop(); return true; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-sd",
    "storage.volume", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver;
}
