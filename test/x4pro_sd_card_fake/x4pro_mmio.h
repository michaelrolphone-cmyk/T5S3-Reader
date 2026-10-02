#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "x4pro_pins.h"
#include "x4pro_proto.h"

extern uint8_t x4_card_sector[512];
extern uint8_t x4_card_partition_boot[512];
extern uint8_t x4_card_fat_sector[512], x4_card_root_sector[512];
extern bool x4_card_bad_crc, x4_card_no_data, x4_card_bad_pin;
extern unsigned x4_card_cmd17_count, x4_card_clock_count;

static uint8_t command_bits[48], response_bits[136];
static unsigned command_count, response_count, response_index, data_index;
static bool response_active, data_active, data_pending;
static const uint8_t *active_sector;

static inline void x4_fake_response(const uint8_t *bytes, unsigned count) {
    response_count = count * 8u;
    response_index = 0;
    response_active = true;
    for (unsigned i = 0; i < response_count; ++i)
        response_bits[i] = (uint8_t)((bytes[i / 8u] >> (7u - i % 8u)) & 1u);
}
static inline void x4_fake_clock(void) {
    ++x4_card_clock_count;
    if (response_active && ++response_index >= response_count) {
        response_active = false;
        if (data_pending && !x4_card_no_data) {
            data_pending = false;
            data_active = true;
            data_index = 0;
        }
    } else if (data_active && ++data_index >= 1u + 512u * 8u + 16u + 1u) {
        data_active = false;
    }
}
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    if (pin == X4PRO_PIN_SD_CLK) { if (!level) x4_fake_clock(); return; }
    if (pin == X4PRO_PIN_SD_CMD) {
        if (command_count < 48u) command_bits[command_count++] = level ? 1u : 0u;
        else x4_card_bad_pin = true;
        return;
    }
    if (pin != X4PRO_PIN_SD_PWR) x4_card_bad_pin = true;
}
static inline void x4pro_pin_release(uint32_t pin) {
    if (pin == X4PRO_PIN_SD_DAT0) return;
    if (pin != X4PRO_PIN_SD_CMD) { x4_card_bad_pin = true; return; }
    if (command_count != 48u) { command_count = 0; return; }
    uint8_t first = 0;
    for (unsigned i = 0; i < 8u; ++i) first = (uint8_t)((first << 1) | command_bits[i]);
    command_count = 0;
    const uint8_t index = first & 0x3fu;
    if (index == 0u) {
        response_active = data_active = data_pending = false;
        return; /* Native CMD0 is response-free. */
    }
    uint8_t reply[17] = {0};
    reply[0] = index;
    if (index == 8u) { reply[3] = 1u; reply[4] = 0xaau; }
    if (index == 41u) { reply[0] = 0x3fu; reply[1] = 0xc0u; }
    if (index == 3u) { reply[1] = 0x12u; reply[2] = 0x34u; }
    if (index == 17u) {
        uint32_t lba = 0;
        for (unsigned i = 8u; i < 40u; ++i) lba = (lba << 1) | command_bits[i];
        active_sector = (lba == 1u) ? x4_card_partition_boot :
                        (lba == 32u || lba == 33u) ? x4_card_fat_sector :
                        (lba == 2080u || lba == 2081u) ? x4_card_root_sector :
                        x4_card_sector;
        ++x4_card_cmd17_count;
        data_pending = true;
    }
    x4_fake_response(reply, index == 2u ? 17u : 6u);
}
static inline bool x4pro_pin_read(uint32_t pin) {
    if (pin == X4PRO_PIN_SD_CMD)
        return !response_active || response_bits[response_index] != 0u;
    if (pin != X4PRO_PIN_SD_DAT0) { x4_card_bad_pin = true; return true; }
    if (!data_active) return true;
    if (data_index == 0u) return false;
    const unsigned bit = data_index - 1u;
    if (bit < 4096u) return (active_sector[bit / 8u] >> (7u - bit % 8u)) & 1u;
    if (bit < 4112u) {
        const uint16_t crc = x4pro_sd_crc16(active_sector, 512u) ^
                             (x4_card_bad_crc ? 1u : 0u);
        return (crc >> (15u - (bit - 4096u))) & 1u;
    }
    return true;
}
