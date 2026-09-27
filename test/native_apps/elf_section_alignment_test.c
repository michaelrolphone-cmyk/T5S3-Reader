#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "private/elf_section_layout.h"

int main(void) {
    assert(elf_section_alignment_valid(0));
    assert(elf_section_alignment_valid(1));
    assert(elf_section_alignment_valid(4));
    assert(elf_section_alignment_valid(8));
    assert(!elf_section_alignment_valid(3));
    assert(!elf_section_alignment_valid(ELF_SECTION_MAX_ALIGNMENT * 2u));

    /*
     * Reproduce the layout shape of i2c-esp32s3-v2 0.1.3:
     * .rodata=23 bytes/alignment 1, .data.rel.ro=60/alignment 4,
     * .bss=208/alignment 8. Start intentionally misaligned too, proving
     * padding is based on the actual runtime pointer rather than file offsets.
     */
    const size_t sizes[] = {23u, 60u, 208u};
    const uint32_t aligns[] = {1u, 4u, 8u};
    size_t capacity = 0;
    for (size_t i = 0; i < 3; ++i)
        assert(elf_section_capacity_add(&capacity, sizes[i], aligns[i]));

    uint8_t storage[512] = {0};
    uint8_t *raw = storage + 3;
    uint8_t *cursor = raw;
    const uintptr_t end = (uintptr_t)raw + capacity;
    for (size_t i = 0; i < 3; ++i) {
        uint8_t *aligned = elf_section_align_pointer(cursor, aligns[i]);
        assert(aligned);
        assert(((uintptr_t)aligned & (aligns[i] - 1u)) == 0u);
        assert((uintptr_t)aligned + sizes[i] <= end);
        cursor = aligned + sizes[i];
    }

    /* The old concatenating layout would put the 8-byte BSS at +83. */
    assert(((uintptr_t)raw + 23u + 60u) % 8u != 0u);
    puts("ELF section alignment helper: odd rodata -> aligned DRLRO/BSS PASS");
    return 0;
}
