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
            uint8_t sector[512];
            if (!read_sector(0, sector)) { fail("sector 0 read failed"); return false; }
            if (sector[510] != 0x55u || sector[511] != 0xaau) {
                fail("sector 0 signature invalid"); return false;
            }
            fail("sector 0 read; filesystem not mounted");
            return true;
        }
        if (clock_api) clock_api->sleep_ms(clock_api->context, 10);
    }
    fail("card idle");
    return false;
}
static bool refresh(void *context) {
    (void)context;
    error[0] = 0;
    if (!started) return false;
    (void)init_card();
    return true;
}
/* A card answering ACMD41 is not yet a mounted, usable filesystem. */
static bool ready(void *context) { (void)context; return false; }
static bool label(void *context, char *out, size_t capacity) {
    (void)context;
    if (!ready(0) || !out || capacity < 6) return false;
    memcpy(out, "X4PRO", 6);
    return true;
}
static bool stat(void *context, const char *path, uint64_t *size_out, bool *is_directory_out) {
    (void)context; (void)path; (void)size_out; (void)is_directory_out;
    fail("filesystem not mounted");
    return false;
}
static risc_storage_dir_t dir_open(void *context, const char *path) { (void)context; (void)path; return RISC_STORAGE_DIR_INVALID; }
static bool dir_next(void *context, risc_storage_dir_t directory, risc_storage_dirent_v1 *entry) {
    (void)context; (void)directory; (void)entry; return false;
}
static void dir_close(void *context, risc_storage_dir_t directory) { (void)context; (void)directory; }
static risc_storage_file_t file_open_read(void *context, const char *path, uint64_t *size_out) {
    (void)context; (void)path; (void)size_out; return RISC_STORAGE_FILE_INVALID;
}
static size_t file_read(void *context, risc_storage_file_t file, void *buffer, size_t capacity) {
    (void)context; (void)file; (void)buffer; (void)capacity; return 0;
}
static risc_storage_file_t file_open_write(void *context, const char *path) { (void)context; (void)path; return RISC_STORAGE_FILE_INVALID; }
static size_t file_write(void *context, risc_storage_file_t file, const void *buffer, size_t size) {
    (void)context; (void)file; (void)buffer; (void)size; return 0;
}
static bool file_close(void *context, risc_storage_file_t file, bool commit) { (void)context; (void)file; (void)commit; return false; }
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
