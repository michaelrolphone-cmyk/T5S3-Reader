#!/usr/bin/env python3
"""Guard production ELF loader integration for per-section sh_addralign."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = (ROOT / "lib/elf_loader/src/esp_elf.c").read_text(encoding="utf-8")
VALIDATE = (ROOT / "lib/elf_loader/src/esp_elf_validate.c").read_text(encoding="utf-8")

assert '#include "private/elf_section_layout.h"' in SRC
assert '#include "private/elf_section_layout.h"' in VALIDATE
assert "section_align[ELF_SEC_RODATA] = shdr[i].addralign;" in SRC
assert "section_align[ELF_SEC_DRLRO] = shdr[i].addralign;" in SRC
assert "section_align[ELF_SEC_BSS] = shdr[i].addralign;" in SRC
assert "elf_section_capacity_add(&size" in SRC
assert "elf_section_align_pointer(pdata, section_align[sec])" in SRC
assert "elf->sec[sec].addr = (uintptr_t)aligned;" in SRC
assert "elf_section_alignment_valid(s[i].addralign)" in VALIDATE

# Prevent regression to the exact byte-concatenating pattern that produced an
# unaligned i2c provider BSS and the on-device LoadStoreAlignment panic.
for forbidden in (
    "pdata += elf->sec[ELF_SEC_RODATA].size;",
    "pdata += elf->sec[ELF_SEC_DRLRO].size;",
):
    assert forbidden not in SRC

print("Production ELF loader section-alignment contracts passed")
