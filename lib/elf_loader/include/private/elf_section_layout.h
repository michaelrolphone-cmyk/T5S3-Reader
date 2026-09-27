#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ET_DYN sections used by RiscRTE providers currently require small natural
 * alignments. Bound sh_addralign so a malformed image cannot turn alignment
 * padding into an unbounded allocation request.
 */
#define ELF_SECTION_MAX_ALIGNMENT 4096u

static inline bool elf_section_alignment_valid(uint32_t alignment)
{
    return alignment == 0u ||
           (alignment <= ELF_SECTION_MAX_ALIGNMENT &&
            (alignment & (alignment - 1u)) == 0u);
}

static inline size_t elf_section_alignment_or_one(uint32_t alignment)
{
    return alignment > 1u ? (size_t)alignment : 1u;
}

static inline bool elf_section_capacity_add(size_t *capacity,
                                            size_t section_size,
                                            uint32_t alignment)
{
    if (!capacity || !elf_section_alignment_valid(alignment)) return false;
    const size_t a = elf_section_alignment_or_one(alignment);
    const size_t padding = a - 1u;
    if (*capacity > SIZE_MAX - padding ||
        *capacity + padding > SIZE_MAX - section_size) return false;
    *capacity += padding + section_size;
    return true;
}

static inline uint8_t *elf_section_align_pointer(uint8_t *pointer,
                                                 uint32_t alignment)
{
    if (!pointer || !elf_section_alignment_valid(alignment)) return NULL;
    const uintptr_t a = (uintptr_t)elf_section_alignment_or_one(alignment);
    const uintptr_t value = (uintptr_t)pointer;
    if (value > UINTPTR_MAX - (a - 1u)) return NULL;
    const uintptr_t aligned = (value + (a - 1u)) & ~(a - 1u);
    return (uint8_t *)aligned;
}
