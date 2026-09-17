/* Physical ESP32-S3 I2C controller provider. The ESP-IDF I2C implementation
 * MUST be linked into this ELF; using i2c_* functions exported by resident
 * firmware would violate the hardware-driver boundary. The generic provider
 * executor serializes all entries. This driver is NOT activated while legacy
 * Wire owns I2C0 and is NOT a release artifact until arbitration is proven. */
#include "RiscI2cBusV1.h"
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <stddef.h>
#include <stdint.h>

#define BUS_PORT I2C_NUM_0
#define BOARD_SDA 39
#define BOARD_SCL 40
#define BOARD_CLOCK_HZ 400000u
#define MAX_CLAIMS 12u
#define MAX_TRANSACTION_BYTES 128u
#define MAX_TIMEOUT_MS 3000u

typedef struct {
    uint64_t token;
    uint8_t address;
} device_claim;
static device_claim claims[MAX_CLAIMS];
static uint64_t next_token;
static bool started, installed, busy, faulted;

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || !started || !installed || faulted || busy ||
        address < 0x08u || address > 0x77u || next_token == UINT64_MAX)
        return false;
    device_claim *empty = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].address == address) return false;
        if (!claims[i].token && !empty) empty = &claims[i];
    }
    if (!empty) return false;
    empty->address = address;
    empty->token = ++next_token;
    *out = empty->token;
    return true;
}

static bool transact(void *context, uint64_t token,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    if (!started || !installed || faulted || busy || !token ||
        (!write_length && !read_length) ||
        write_length > MAX_TRANSACTION_BYTES ||
        read_length > MAX_TRANSACTION_BYTES ||
        (write_length && !write_bytes) || (read_length && !read_bytes) ||
        !timeout_ms || timeout_ms > MAX_TIMEOUT_MS) return false;
    const device_claim *found = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token == token) { found = &claims[i]; break; }
    if (!found) return false;
    /* Synchronous IDF calls issue one transaction; the combined variant
     * explicitly uses a repeated START between the write and read phases. */
    busy = true;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (!ticks) ticks = 1;
    esp_err_t result;
    if (write_length && read_length)
        result = i2c_master_write_read_device(BUS_PORT, found->address,
                    write_bytes, write_length, read_bytes, read_length, ticks);
    else if (write_length)
        result = i2c_master_write_to_device(BUS_PORT, found->address,
                    write_bytes, write_length, ticks);
    else
        result = i2c_master_read_from_device(BUS_PORT, found->address,
                    read_bytes, read_length, ticks);
    busy = false;
    /* A NACK, a busy bus or timeout is a reported transaction failure, not
     * permission to fabricate a successful charger register read/write. */
    return result == ESP_OK;
}

static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!started || !installed || faulted || busy || !token) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            claims[i].token = 0;
            claims[i].address = 0;
            return true;
        }
    }
    return false;
}

static bool quiesce(void) {
    if (busy) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;
    /* A failed setup without an installed controller may have changed pins;
     * there is no demonstrated safe recovery yet. Keep its ELF quarantined. */
    if (faulted && !installed) return false;
    if (installed) {
        /* Permit an explicit retry when the IDF delete previously failed.
         * The module remains mapped until a successful hardware teardown. */
        if (i2c_driver_delete(BUS_PORT) != ESP_OK) {
            faulted = true;
            return false;
        }
        installed = false;
    }
    faulted = false;
    started = false;
    return true;
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    (void)dependencies;
    if (dependency_count || started || installed || busy || faulted) return false;
    /* The driver must not be started until the generic platform ownership
     * cutover has excluded the compiled Wire/I2C implementation. The IDF
     * library is bundled, so its private static state CANNOT detect Wire's
     * separate static owner. Manifest and production installer block use. */
    i2c_config_t config = {0};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = BOARD_SDA;
    config.scl_io_num = BOARD_SCL;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = BOARD_CLOCK_HZ;
    if (i2c_param_config(BUS_PORT, &config) != ESP_OK) {
        faulted = true; /* No proof that partial pin configuration is safe. */
        return false;
    }
    if (i2c_driver_install(BUS_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
        faulted = true;
        return false;
    }
    installed = true;
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }

static const risc_i2c_bus_api_v1 bus_api = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
    claim_device, transact, release_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "i2c-esp32s3-v2", "i2c.bus", RISC_I2C_BUS_API_V1,
    &bus_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
