#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_platform.h"
#include "cache_test_stubs.h"

/* Model the two aliases of newly loaded code. The surrounding allocation is
 * live LCD state, which must never be included in loader cache maintenance.
 * This checks the publication contract, not the silicon erratum itself. */
enum { SIZE = 70001 };
static unsigned char data_cache[SIZE], psram[SIZE], instruction_cache[SIZE];
static uint32_t start, length, pending_offset, pending_size;
static unsigned writes, invalidates, yields, locked, fail_write, fail_invalidate;
static TickType_t ticks, tick_step;

void test_enter(portMUX_TYPE *lock) { (void)lock; assert(!locked); locked = 1; }
void test_exit(portMUX_TYPE *lock) { (void)lock; assert(locked); locked = 0; }
TickType_t xTaskGetTickCount(void) { ticks += tick_step; return ticks; }
void vTaskDelay(TickType_t delay) { assert(!locked && delay == 1); ++yields; }
void *heap_caps_malloc(size_t size, uint32_t caps) { (void)size; (void)caps; return NULL; }
void heap_caps_free(void *ptr) { (void)ptr; }

int Cache_WriteBack_Addr(uint32_t addr, uint32_t size)
{
    assert(locked && size && size <= 4096);
    assert(addr >= start && addr - start <= length && size <= length - (addr - start));
    assert(writes == invalidates);
    if (++writes == fail_write) return 1;
    pending_offset = addr - start;
    pending_size = size;
    memcpy(psram + pending_offset, data_cache + pending_offset, size);
    return 0;
}

int Cache_Invalidate_Addr(uint32_t addr, uint32_t size)
{
    assert(locked && writes == invalidates + 1);
    assert(addr == start + (SOC_IROM_LOW - SOC_DROM_LOW) + pending_offset);
    assert(size == pending_size);
    if (++invalidates == fail_invalidate) return 1;
    memcpy(instruction_cache + pending_offset, psram + pending_offset, size);
    return 0;
}

/* A full-cache operation would touch concurrently mutable display state. */
void esp_spiram_writeback_cache(void) { assert(!"unrelated live cache writeback"); }
void Cache_WriteBack_All(void) { assert(!"unrelated live cache writeback"); }
void spi_flash_disable_interrupts_caches_and_other_cpu(void) { assert(!"global cache disable"); }
void spi_flash_enable_interrupts_caches_and_other_cpu(void) { assert(!"global cache disable"); }

static esp_elf_t reset(uint32_t addr, uint32_t size)
{
    esp_elf_t elf = {0};
    start = addr; length = size;
    elf.sec[ELF_SEC_TEXT].addr = addr;
    elf.sec[ELF_SEC_TEXT].size = size;
    writes = invalidates = yields = locked = fail_write = fail_invalidate = 0;
    ticks = tick_step = 0;
    memset(psram, 0x11, sizeof(psram));
    memset(instruction_cache, 0x22, sizeof(instruction_cache));
    memset(data_cache, 0x33, sizeof(data_cache));
    return elf;
}

int main(void)
{
    /* Exact unaligned allocation, with stale instructions from its prior owner. */
    esp_elf_t elf = reset(SOC_DROM_LOW + 0x10003, SIZE);
    assert(esp_elf_arch_flush(&elf) == 0);
    assert(!memcmp(instruction_cache, data_cache, SIZE));
    assert(writes == invalidates && yields == 2 && !locked);

    /* Time checkpoint also yields before reaching the byte checkpoint. */
    elf = reset(start, 8193);
    ticks = UINT32_MAX - 2; tick_step = 2;
    assert(esp_elf_arch_flush(&elf) == 0 && yields == 2);

    elf = reset(start, 1);
    assert(esp_elf_arch_flush(&elf) == 0 && writes == 1 && !yields);
    elf = reset(start, SIZE); fail_write = 2;
    assert(esp_elf_arch_flush(&elf) == -EIO && invalidates == 1 && !locked);
    elf = reset(start, SIZE); fail_invalidate = 2;
    assert(esp_elf_arch_flush(&elf) == -EIO && writes == 2 && !locked);

    elf = reset(SOC_DROM_HIGH - 4, 8);
    assert(esp_elf_arch_flush(&elf) == -EINVAL && !writes);
    elf = reset(SOC_DROM_LOW - 1, 1);
    assert(esp_elf_arch_flush(&elf) == -EINVAL && !writes);
    elf = reset(start, 0);
    assert(esp_elf_arch_flush(&elf) == -EINVAL && !writes);
    assert(esp_elf_arch_flush(NULL) == -EINVAL);
    puts("ELF code cache publication tests passed");
}
