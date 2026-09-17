#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_manifest_imports.h"

/* No executable code is run: this probes the exact-set gate separately from
 * signature verification and the full ELF structure validator. */
typedef union { uint32_t align; uint8_t bytes[1024]; } fixture_t;

int main(void)
{
    fixture_t fixture = {0};
    uint8_t *bytes = fixture.bytes;
    elf32_hdr_t *header = (elf32_hdr_t *)bytes;
    header->shoff = 640;
    header->shnum = 5;
    header->shentsize = sizeof(elf32_shdr_t);
    elf32_shdr_t *sections = (elf32_shdr_t *)(bytes + header->shoff);
    sections[1].type = SHT_SYNSYM;
    sections[1].offset = 256;
    sections[1].size = 3 * sizeof(elf32_sym_t);
    sections[1].link = 2;
    sections[2].type = SHT_STRTAB;
    sections[2].offset = 128;
    sections[2].size = 128;
    sections[3].type = SHT_SYMTAB;
    sections[3].offset = 320;
    sections[3].size = 3 * sizeof(elf32_sym_t);
    sections[3].link = 4;
    sections[4].type = SHT_STRTAB;
    sections[4].offset = 384;
    sections[4].size = 128;
    char *dyn_names = (char *)(bytes + 128);
    char *static_names = (char *)(bytes + 384);
    strcpy(dyn_names + 1, "esp_intr_alloc");
    strcpy(dyn_names + 32, "malloc");
    strcpy(static_names + 1, "malloc");
    strcpy(static_names + 32, "memcpy");
    elf32_sym_t *dyn = (elf32_sym_t *)(bytes + 256);
    elf32_sym_t *ordinary = (elf32_sym_t *)(bytes + 320);
    dyn[1].name = 1;
    dyn[2].name = 32;
    ordinary[1].name = 1;
    ordinary[2].name = 32;
    dyn[1].info = dyn[2].info = ordinary[1].info = ordinary[2].info =
        ELF_ST_INFO(STB_GLOBAL, STT_FUNC);

    const char *const exact[] = {"esp_intr_alloc", "malloc", "memcpy"};
    assert(esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                         exact, 3));
    const char *const dyn_only[] = {"esp_intr_alloc", "malloc"};
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          dyn_only, 2));
    const char *const unneeded[] = {"esp_intr_alloc", "malloc", "memcpy", "printf"};
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          unneeded, 4));
    const char *const duplicate[] = {"esp_intr_alloc", "malloc", "malloc", "memcpy"};
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          duplicate, 4));
    const char *const unsorted[] = {"malloc", "esp_intr_alloc", "memcpy"};
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          unsorted, 3));
    const char *const empty[] = {"", "esp_intr_alloc", "malloc", "memcpy"};
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          empty, 4));
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          NULL, 3));
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          exact, 0));
    assert(!esp_elf_privileged_manifest_imports_match_v1(NULL, sizeof(fixture.bytes),
                                                          exact, 3));

    /* An otherwise globally permitted symbol still fails if it was not in
     * this specific signed manifest's exact import declaration. */
    strcpy(static_names + 32, "printf");
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          exact, 3));
    strcpy(static_names + 32, "memcpy");
    assert(esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                         exact, 3));
    strcpy(static_names + 32, "usb_host_install");
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          exact, 3));
    strcpy(static_names + 32, "memcpy");
    ordinary[2].name = 128;
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                          exact, 3));
    ordinary[2].name = 32;
    assert(esp_elf_privileged_manifest_imports_match_v1(bytes, sizeof(fixture.bytes),
                                                         exact, 3));
    puts("Privileged manifest imports: exact, sorted, complete, duplicated-table and tamper checks PASS");
    return 0;
}
