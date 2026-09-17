/* Host fixture: calls the ACTUAL physical I2C provider, substitutes IDF
 * functions only for assertions. This is not an installable hardware test. */
#include "RiscI2cBusV1.h"
#include <driver/i2c.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned configurations, installs, deletions, reads, writes, combined;
static unsigned fail_config, fail_install, fail_delete, fail_transaction;
static bool installed;
static uint8_t last_address;
static TickType_t last_ticks;

esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *cfg) {
    assert(port == I2C_NUM_0 && cfg && cfg->mode == I2C_MODE_MASTER);
    assert(cfg->sda_io_num == 39 && cfg->scl_io_num == 40);
    assert(cfg->master.clk_speed == 400000 && cfg->sda_pullup_en && cfg->scl_pullup_en);
    ++configurations;
    if (fail_config) { --fail_config; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t i2c_driver_install(i2c_port_t port, i2c_mode_t mode,
                             size_t rx, size_t tx, int intr_flags) {
    assert(port == I2C_NUM_0 && mode == I2C_MODE_MASTER);
    assert(!rx && !tx && !intr_flags && !installed);
    ++installs;
    if (fail_install) { --fail_install; return ESP_FAIL; }
    installed = true;
    return ESP_OK;
}
esp_err_t i2c_driver_delete(i2c_port_t port) {
    assert(port == I2C_NUM_0 && installed);
    ++deletions;
    if (fail_delete) { --fail_delete; return ESP_FAIL; }
    installed = false;
    return ESP_OK;
}
static esp_err_t transaction(uint8_t address, TickType_t ticks) {
    assert(installed && address == 0x6b && ticks == 100);
    last_address = address;
    last_ticks = ticks;
    if (fail_transaction) { --fail_transaction; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t i2c_master_write_read_device(i2c_port_t port, uint8_t address,
                    const uint8_t *wr, size_t nwr, uint8_t *rd, size_t nrd,
                    TickType_t ticks) {
    assert(port == I2C_NUM_0 && wr && rd && nwr == 1 && nrd == 1 && wr[0] == 3);
    ++combined;
    esp_err_t result = transaction(address, ticks);
    if (result == ESP_OK) rd[0] = 0x20;
    return result;
}
esp_err_t i2c_master_write_to_device(i2c_port_t port, uint8_t address,
                    const uint8_t *wr, size_t nwr, TickType_t ticks) {
    assert(port == I2C_NUM_0 && wr && nwr == 2 && wr[0] == 3);
    ++writes;
    return transaction(address, ticks);
}
esp_err_t i2c_master_read_from_device(i2c_port_t port, uint8_t address,
                    uint8_t *rd, size_t nrd, TickType_t ticks) {
    assert(port == I2C_NUM_0 && rd && nrd == 1);
    ++reads;
    esp_err_t result = transaction(address, ticks);
    if (result == ESP_OK) rd[0] = 0x42;
    return result;
}

int main(void) {
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && !t5_driver_get(1));
    assert(strcmp(driver->driver_id, "i2c-esp32s3-v2") == 0);
    assert(strcmp(driver->capability_id, "i2c.bus") == 0);
    assert(driver->quiesce && driver->capability);
    const risc_i2c_bus_api_v1 *bus = driver->capability;
    assert(bus->api_version == 1 && bus->struct_size == sizeof(*bus));
    assert(!driver->start(NULL, 1));
    assert(driver->start(NULL, 0));
    assert(installed && configurations == 1 && installs == 1);
    assert(!driver->start(NULL, 0));
    uint64_t claim = UINT64_MAX, other = 0;
    assert(!bus->claim_device(NULL, 7, &claim) && !claim);
    assert(!bus->claim_device(NULL, 0x78, &claim) && !claim);
    assert(bus->claim_device(NULL, 0x6b, &claim) && claim);
    assert(!bus->claim_device(NULL, 0x6b, &other) && !other);
    assert(bus->claim_device(NULL, 0x55, &other) && other != claim);
    assert(!driver->quiesce());
    uint8_t reg = 3, answer = 0, command[2] = {3, 0x20};
    assert(!bus->transact(NULL, other + 12, &reg, 1, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 0));
    assert(!bus->transact(NULL, claim, &reg, 129, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, NULL, 1, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, NULL, 0, NULL, 0, 100));
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 3001));
    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(combined == 1 && answer == 0x20 && last_address == 0x6b && last_ticks == 100);
    assert(bus->transact(NULL, claim, command, 2, NULL, 0, 100) && writes == 1);
    assert(bus->transact(NULL, claim, NULL, 0, &answer, 1, 100) && reads == 1 && answer == 0x42);
    fail_transaction = 1;
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(!bus->release_device(NULL, claim + 99));
    assert(bus->release_device(NULL, other));
    assert(bus->release_device(NULL, claim));
    assert(!bus->release_device(NULL, claim));
    fail_delete = 1;
    assert(!driver->quiesce() && installed);
    assert(driver->quiesce() && !installed); /* physical teardown retry */
    driver->stop();
    assert(driver->start(NULL, 0));
    uint64_t fresh = 0;
    assert(bus->claim_device(NULL, 0x6b, &fresh) && fresh > claim);
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->release_device(NULL, fresh));
    assert(driver->quiesce());
    driver->stop();
    fail_install = 1;
    assert(!driver->start(NULL, 0));
    assert(!driver->quiesce()); /* configuration modified pins; quarantine */
    assert(configurations == 3 && installs == 3 && deletions == 3);
    puts("Physical I2C provider: real IDF entry calls, repeated START, claim generations, bounds and safe teardown: PASS");
    return 0;
}
