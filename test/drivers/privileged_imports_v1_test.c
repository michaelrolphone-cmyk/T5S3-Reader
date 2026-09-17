#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_imports.h"

/* Synthetic bounded tables: no ELF code is executed. Structural checks run
 * independently; the private loader executes both validators before mapping. */
typedef union {
    uint32_t alignment;
    uint8_t bytes[1024];
} fixture_t;

int main(void)
{
    fixture_t fixture = {0};
    uint8_t *bytes = fixture.bytes;
    const size_t length = sizeof(fixture.bytes);
    elf32_hdr_t *header = (elf32_hdr_t *)bytes;
    header->shoff = 512;
    header->shnum = 3;
    header->shentsize = sizeof(elf32_shdr_t);
    elf32_shdr_t *sections = (elf32_shdr_t *)(bytes + header->shoff);
    sections[1].type = SHT_SYNSYM;
    sections[1].offset = 256;
    sections[1].size = 3 * sizeof(elf32_sym_t);
    sections[1].link = 2;
    sections[2].type = SHT_STRTAB;
    sections[2].offset = 128;
    sections[2].size = 128;
    char *names = (char *)(bytes + sections[2].offset);
    strcpy(names + 1, "esp_intr_alloc");
    strcpy(names + 32, "malloc");
    elf32_sym_t *symbols = (elf32_sym_t *)(bytes + sections[1].offset);
    symbols[1].name = 1;
    symbols[1].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    symbols[2].name = 32;
    symbols[2].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);

    assert(esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(names + 1, "__stack_chk_guard");
    symbols[1].info = ELF_ST_INFO(STB_GLOBAL, STT_OBJECT);
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(names + 1, "usb_host_install");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(names + 1, "i2c_driver_install");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(names + 1, "t5_usb_get_api");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(names + 1, "esp_intr_alloc");
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));

    symbols[1].name = 128;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    symbols[1].name = 1;
    symbols[1].info = ELF_ST_INFO(STB_LOCAL, STT_FUNC);
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    symbols[1].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    sections[1].link = 3;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[1].link = 2;
    sections[1].offset = 1020;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[1].offset = 256;
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));

    /* Move section headers and add a second, independent .symtab, its own
     * string table and a RELA targeting .symtab. The old dynsym is still
     * clean, reproducing the former policy bypass exactly. */
    memmove(bytes + 640, bytes + 512, 3 * sizeof(elf32_shdr_t));
    header->shoff = 640;
    header->shnum = 6;
    sections = (elf32_shdr_t *)(bytes + header->shoff);
    sections[3].type = SHT_SYMTAB;
    sections[3].offset = 320;
    sections[3].size = 3 * sizeof(elf32_sym_t);
    sections[3].link = 4;
    sections[4].type = SHT_STRTAB;
    sections[4].offset = 384;
    sections[4].size = 128;
    sections[5].type = SHT_RELA;
    sections[5].offset = 520;
    sections[5].size = sizeof(elf32_rela_t);
    sections[5].link = 3;
    char *alternate_names = (char *)(bytes + sections[4].offset);
    strcpy(alternate_names + 1, "malloc");
    strcpy(alternate_names + 32, "memcpy");
    elf32_sym_t *alternate = (elf32_sym_t *)(bytes + sections[3].offset);
    alternate[1].name = 1;
    alternate[1].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    alternate[2].name = 32;
    alternate[2].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    elf32_rela_t *rel = (elf32_rela_t *)(bytes + sections[5].offset);
    rel[0].info = ELF_R_INFO(2, 2);
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));

    strcpy(alternate_names + 32, "usb_host_install");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(alternate_names + 32, "i2c_driver_install");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(alternate_names + 32, "t5_usb_get_api");
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    strcpy(alternate_names + 32, "memcpy");
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));

    alternate[2].name = 128;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    alternate[2].name = 32;
    alternate[2].info = ELF_ST_INFO(STB_LOCAL, STT_FUNC);
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    alternate[2].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    sections[3].link = 6;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[3].link = 4;
    sections[3].offset = 1020;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[3].offset = 320;
    rel[0].info = ELF_R_INFO(3, 2);
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    rel[0].info = ELF_R_INFO(2, 2);
    sections[5].link = 4;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[5].link = 3;
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[3].type = SHT_SYNSYM; /* No second dynsym. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[3].type = SHT_SYMTAB;
    sections[1].type = SHT_SYMTAB; /* No dynsym. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[1].type = SHT_SYNSYM;
    sections[5].type = SHT_REL; /* Unsupported implicit-addend format. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[5].type = SHT_RELA;
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));
    header->shoff = 1016;
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    assert(!esp_elf_privileged_imports_valid_v1(NULL, length));
    puts("Privileged provider import preflight: dynsym, symtab, RELA and bounds PASS");
    return 0;
}
