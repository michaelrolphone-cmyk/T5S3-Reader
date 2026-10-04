#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x4pro_pins.h"
#include "x4pro_proto.h"
extern uint8_t *card_image;
extern uint32_t card_sectors;
extern bool card_bad_crc, card_reject_write, card_busy_forever, card_bad_pin;
extern unsigned card_reads, card_writes;
extern bool card_sleep_off;
extern bool card_power_off;
extern unsigned card_sleep_commits;
static uint8_t cmd_bits[48], reply_bits[136], write_bytes[512];
static unsigned cmd_count, reply_count, reply_at, data_at, write_at, token_at;
static uint16_t write_crc;
static uint32_t data_lba;
static bool reply_active, read_active, read_pending, write_pending, token_active;
static void fake_tick(void) {
    if (reply_active) {
        if (++reply_at >= reply_count) { reply_active = false; if (read_pending) { read_pending = false; read_active = true; data_at = 0; } }
    } else if (read_active) { if (++data_at >= 4114) read_active = false; }
    else if (token_active) { if (++token_at >= 7 && !card_busy_forever) token_active = false; }
}
static inline void x4pro_pin_output(uint32_t pin, bool high) {
    if (pin == X4PRO_PIN_SD_CLK) { if (!high) fake_tick(); return; }
    if (pin == X4PRO_PIN_SD_CMD) { if (cmd_count < 48) cmd_bits[cmd_count++] = high; else card_bad_pin = true; return; }
    if (pin == X4PRO_PIN_SD_PWR) {
        // A held output cannot be changed by writing GPIO or IO_MUX registers.
        if (!card_sleep_off) card_power_off = high;
        return;
    }
    if (pin == X4PRO_PIN_SD_DAT0 && write_pending) {
        if (!write_at) { if (high) card_bad_pin = true; memset(write_bytes, 0, sizeof(write_bytes)); write_crc = 0; }
        else if (write_at <= 4096) { unsigned bit = write_at - 1; write_bytes[bit / 8] |= (uint8_t)high << (7 - bit % 8); }
        else if (write_at <= 4112) write_crc = (uint16_t)((write_crc << 1) | high);
        else if (write_at == 4113 && !high) card_bad_pin = true;
        ++write_at; return;
    }
    card_bad_pin = true;
}
static inline void x4pro_pin_release(uint32_t pin) {
    if (pin == X4PRO_PIN_SD_DAT0) {
        if (write_pending && write_at) {
            if (write_at != 4114 || write_crc != x4pro_sd_crc16(write_bytes, 512)) card_bad_pin = true;
            if (!card_reject_write && data_lba < card_sectors) memcpy(card_image + (size_t)data_lba * 512, write_bytes, 512);
            ++card_writes; write_pending = false; token_active = true; token_at = 0;
        }
        return;
    }
    if (pin != X4PRO_PIN_SD_CMD) { card_bad_pin = true; return; }
    if (cmd_count != 48) { cmd_count = 0; return; }
    unsigned index = 0; uint32_t arg = 0;
    for (unsigned i = 0; i < 8; ++i) index = (index << 1) | cmd_bits[i];
    for (unsigned i = 8; i < 40; ++i) arg = (arg << 1) | cmd_bits[i];
    cmd_count = 0; index &= 63;
    if (!index) { reply_active = read_active = read_pending = write_pending = token_active = false; return; }
    uint8_t reply[17] = {0}; reply[0] = index;
    if (index == 8) { reply[3] = 1; reply[4] = 0xaa; }
    if (index == 41) { reply[0] = 0x3f; reply[1] = 0xc0; }
    if (index == 3) { reply[1] = 0x12; reply[2] = 0x34; }
    if (index == 17 || index == 24) {
        data_lba = arg;
        if (data_lba >= card_sectors) { card_bad_pin = true; data_lba = 0; }
        if (index == 17) { ++card_reads; read_pending = true; }
        else { write_pending = true; write_at = 0; }
    }
    reply_count = index == 2 ? 136 : 48; reply_at = 0; reply_active = true;
    for (unsigned i = 0; i < reply_count; ++i) reply_bits[i] = (reply[i / 8] >> (7 - i % 8)) & 1;
}
static inline bool x4pro_pin_read(uint32_t pin) {
    // Model an unpowered card with CMD/DAT low. Physical floating levels may
    // instead time out; neither is a valid card response. No file was read.
    if (card_power_off && (pin == X4PRO_PIN_SD_CMD || pin == X4PRO_PIN_SD_DAT0)) return false;
    if (pin == X4PRO_PIN_SD_CMD) return !reply_active || reply_bits[reply_at];
    if (pin != X4PRO_PIN_SD_DAT0) { card_bad_pin = true; return true; }
    if (token_active) {
        if (token_at < 5) return ((card_reject_write ? 11u : 5u) >> (4 - token_at)) & 1;
        return token_at >= 6 && !card_busy_forever;
    }
    if (!read_active) return true;
    if (!data_at) return false;
    const uint8_t *data = card_image + (size_t)data_lba * 512;
    unsigned bit = data_at - 1;
    if (bit < 4096) return (data[bit / 8] >> (7 - bit % 8)) & 1;
    if (bit < 4112) return ((x4pro_sd_crc16(data, 512) ^ (card_bad_crc ? 1 : 0)) >> (15 - (bit - 4096))) & 1;
    return true;
}

static inline void x4pro_pin_input(uint32_t pin, bool pullup) {
    if ((pin != X4PRO_PIN_SD_CMD && pin != X4PRO_PIN_SD_DAT0) || pullup) card_bad_pin = true;
}
static inline void x4pro_pin_hold(uint32_t pin, bool hold) {
    if (pin != X4PRO_PIN_SD_PWR) card_bad_pin = true;
    card_sleep_off = hold;
    if (hold) ++card_sleep_commits;
}

uint32_t x4pro_sd_test_cycle_count(void);
#define X4PRO_SD_CYCLE_COUNT() x4pro_sd_test_cycle_count()
