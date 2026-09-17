#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_imports.h"

/* A bounded synthetic dynamic-symbol table. The full ELF structural validator
 * is separately exercised by native-app tests and precedes this preflight in
 * the production entry point. This fixture never executes any ELF code. */
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

    symbols[1].name = 128; /* Outside the string table. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    symbols[1].name = 1;
    symbols[1].info = ELF_ST_INFO(STB_LOCAL, STT_FUNC);
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    symbols[1].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    sections[1].link = 3; /* Out-of-range section index. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[1].link = 2;
    sections[1].offset = 1020; /* Symbol array crosses the buffer end. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    sections[1].offset = 256;
    assert(esp_elf_privileged_imports_valid_v1(bytes, length));
    header->shoff = 1016; /* Section table crosses the buffer end. */
    assert(!esp_elf_privileged_imports_valid_v1(bytes, length));
    assert(!esp_elf_privileged_imports_valid_v1(NULL, length));
    puts("Privileged provider ELF import preflight: allowlist and bounds PASS");
    return 0;
}
