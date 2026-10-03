/* X4 Pro I2C bus. Owns GPIO 39/38. Upstream drivers bind i2c.bus only. */
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_CLAIMS 8u
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t tokens[MAX_CLAIMS];
static uint8_t addresses[MAX_CLAIMS];
static uint64_t next_token = 1;
static bool started;
static char last_error_text[64];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(last_error_text)) { last_error_text[i] = text[i]; ++i; }
    last_error_text[i] = 0;
}
static void delay(void) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, 0);
    for (volatile int i = 0; i < 40; ++i) {}
}
static void sda(bool high) { high ? x4pro_pin_release(X4PRO_PIN_I2C_SDA) : x4pro_pin_output(X4PRO_PIN_I2C_SDA, false); }
static void scl(bool high) { high ? x4pro_pin_release(X4PRO_PIN_I2C_SCL) : x4pro_pin_output(X4PRO_PIN_I2C_SCL, false); }
static bool write_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        sda((value >> bit) & 1);
        delay(); scl(true); delay(); scl(false);
    }
    sda(true); delay(); scl(true); delay();
    bool ack = !x4pro_pin_read(X4PRO_PIN_I2C_SDA);
    scl(false);
    return ack;
}
static uint8_t read_byte(bool ack) {
    uint8_t value = 0;
    sda(true);
    for (int bit = 7; bit >= 0; --bit) {
        scl(true); delay();
        if (x4pro_pin_read(X4PRO_PIN_I2C_SDA)) value |= (uint8_t)(1u << bit);
        scl(false); delay();
    }
    sda(!ack); delay(); scl(true); delay(); scl(false); sda(true);
    return value;
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!started || !out || address < 0x08u || address > 0x77u) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (tokens[i] && addresses[i] == address) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (!tokens[i]) {
        addresses[i] = address;
        tokens[i] = next_token++;
        *out = tokens[i];
        return true;
    }
    return false;
}
static int find_claim(uint64_t token) {
    if (!token) return -1;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (tokens[i] == token) return (int)i;
    return -1;
}
static bool transact(void *context, uint64_t token, const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length, uint32_t timeout_ms) {
    (void)context; (void)timeout_ms;
    int slot = find_claim(token);
    if (slot < 0) return false;
    scl(true); sda(true); delay();
    sda(false); delay(); scl(false);
    if (!write_byte((uint8_t)(addresses[slot] << 1))) { fail("i2c nack"); sda(false); scl(true); delay(); sda(true); return false; }
    for (size_t i = 0; i < write_length; ++i)
        if (!write_byte(write_bytes[i])) { fail("i2c write nack"); sda(false); scl(true); delay(); sda(true); return false; }
    if (read_length) {
        sda(true); scl(true); delay(); sda(false); delay(); scl(false);
        if (!write_byte((uint8_t)((addresses[slot] << 1) | 1u))) { fail("i2c read nack"); return false; }
        for (size_t i = 0; i < read_length; ++i) read_bytes[i] = read_byte(i + 1u < read_length);
    }
    sda(false); delay(); scl(true); delay(); sda(true);
    return true;
}
static bool release_device(void *context, uint64_t token) {
    (void)context;
    int slot = find_claim(token);
    if (slot < 0) return false;
    tokens[slot] = 0;
    return true;
}
static const risc_i2c_bus_api_v1 api = {
    RISC_I2C_BUS_API_V1, sizeof(api), 0, claim_device, transact, release_device
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    clock_api = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1)
            clock_api = dependencies[i].api;
    }
    if (!clock_api) { fail("platform.clock missing"); return false; }
    x4pro_pin_release(X4PRO_PIN_I2C_SDA);
    x4pro_pin_release(X4PRO_PIN_I2C_SCL);
    started = true;
    return true;
}
static void stop(void) { started = false; clock_api = 0; }
static bool quiesce(void) { stop(); return true; }
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (last_error_text[i] && i + 1u < capacity) { destination[i] = last_error_text[i]; ++i; }
    destination[i] = 0;
    return true;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-i2c",
      "i2c.bus", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
