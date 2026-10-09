// FAT card/MMIO boundary shared with test/storage_volume/inventory_test.cpp.
// This supplies transport only; registration and package payloads are real.
#pragma once
#include "os_cpu_fake.h"
extern "C" {
uint8_t *card_image;
uint32_t card_sectors = 131072;
bool card_bad_crc, card_reject_write, card_busy_forever, card_bad_pin;
unsigned card_reads, card_writes;
bool card_sleep_off;
bool card_power_off;
unsigned card_sleep_commits;
uint64_t card_time;
unsigned inventory_sector_ms;
uint64_t sd_clock_mux_writes, sd_clock_direction_writes, sd_clock_levels;
uint32_t sd_clock_cycles;
unsigned sd_clock_cycle_calls, sd_clock_wraps;
bool sd_clock_counter_stuck, sd_clock_readback_stuck, sd_clock_check_phases;
uint32_t x4pro_sd_test_cycle_count(void) {
    ++sd_clock_cycle_calls;
    if (!sd_clock_counter_stuck) {
        const uint32_t before = sd_clock_cycles;
        sd_clock_cycles += 8;
        if (sd_clock_cycles < before) ++sd_clock_wraps;
    }
    return sd_clock_cycles;
}
const risc_driver_v2 *t5_driver_get(uint32_t);
}
#ifdef TEST_SPI_TRANSPORT
#include "spi_card_model.h"
#endif
static uint64_t now(void*) { return card_time; }
static unsigned sleeps;
static void sleep(void*, uint32_t ms) { ++sleeps; card_time += ms; }
static void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;++i) p[i] = n >> (i*8); }
static void format(bool partitioned) {
    std::memset(card_image, 0, (size_t)card_sectors * 512);
    uint32_t base = partitioned ? 2048 : 0;
    if (base) { card_image[446+4] = 0x0c; put32(card_image+446+8,base); put32(card_image+446+12,card_sectors-base); card_image[510]=0x55;card_image[511]=0xaa; }
    uint8_t *b=card_image+(size_t)base*512;
    b[0]=0xeb; b[1]=0x58;b[2]=0x90;std::memcpy(b+3,"MSDOS5.0",8);
    put16(b+11,512); b[13]=1;put16(b+14,32);b[16]=2;b[21]=0xf8;
    put32(b+32,card_sectors-base);put32(b+36,1024);put32(b+44,2);put16(b+48,1);
    b[66]=0x29;std::memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
    for(unsigned fat=0;fat<2;++fat) { uint8_t *p=card_image+(size_t)(base+32+fat*1024)*512;put32(p,0xffffff8);put32(p+4,0xffffffff);put32(p+8,0xfffffff); }
}

static const risc_storage_volume_api_v1* inspectorApi;
static const risc_storage_volume_api_v1_ext* inspectorExt;
