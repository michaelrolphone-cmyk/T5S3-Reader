#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_elf.h"

/* Exercise the production private entry, mocking native loader/RTOS only. */
static unsigned begins, ends, inits, grants, relocations, deinitializations;
static bool allow_scope = true, allow_grant = true;
static const void *authorized_module;
bool esp_elf_validate_file(const uint8_t *image, size_t length) {
    return image && length == 1024;
}
bool esp_elf_privileged_os_cpu_begin_v1(void) {
    ++begins;
    return allow_scope;
}
bool esp_elf_privileged_os_cpu_end_v1(void) {
    ++ends;
    return true;
}
bool esp_elf_privileged_os_cpu_authorize_relocation_v1(const void *module) {
    ++grants;
    authorized_module = allow_grant ? module : NULL;
    return allow_grant;
}
int esp_elf_init(esp_elf_t *image) { ++inits; memset(image, 0, sizeof(*image)); return 0; }
int esp_elf_relocate(esp_elf_t *image, const uint8_t *bytes) {
    (void)bytes;
    assert(authorized_module == image); /* grant precedes native mapping */
    ++relocations;
    return -ENOSYS; /* Never execute a synthetic image. */
}
void esp_elf_deinit(esp_elf_t *image) { (void)image; ++deinitializations; }

typedef union { uint32_t align; uint8_t bytes[1024]; } fixture_t;
int main(void) {
    fixture_t fixture = {0};
    uint8_t *bytes = fixture.bytes;
    elf32_hdr_t *header = (elf32_hdr_t *)bytes;
    header->shoff = 512;
    header->shnum = 3;
    header->shentsize = sizeof(elf32_shdr_t);
    elf32_shdr_t *sections = (elf32_shdr_t *)(bytes + 512);
    sections[1].type = SHT_SYNSYM;
    sections[1].offset = 256;
    sections[1].size = 3 * sizeof(elf32_sym_t);
    sections[1].link = 2;
    sections[2].type = SHT_STRTAB;
    sections[2].offset = 128;
    sections[2].size = 128;
    char *names = (char *)(bytes + 128);
    strcpy(names + 1, "esp_intr_alloc");
    strcpy(names + 32, "malloc");
    elf32_sym_t *symbols = (elf32_sym_t *)(bytes + 256);
    symbols[1].name = 1;
    symbols[2].name = 32;
    symbols[1].info = symbols[2].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
    const char *const exact[] = {"esp_intr_alloc", "malloc"};
    const char *const incomplete[] = {"esp_intr_alloc"};
    const char *const extra[] = {"esp_intr_alloc", "malloc", "printf"};
    const char *const unordered[] = {"malloc", "esp_intr_alloc"};
    esp_elf_t module = {0};

    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   incomplete, 1) == -EINVAL);
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   extra, 3) == -EINVAL);
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   unordered, 2) == -EINVAL);
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   NULL, 0) == -EINVAL);
    assert(!begins && !inits && !grants && !relocations);
    strcpy(names + 32, "usb_host_install");
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   exact, 2) == -EINVAL);
    strcpy(names + 32, "malloc");
    assert(!begins && !inits && !grants && !relocations);

    allow_scope = false;
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   exact, 2) == -EBUSY);
    assert(begins == 1 && !ends && !inits && !grants && !relocations);
    allow_scope = true;
    allow_grant = false;
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   exact, 2) == -EPERM);
    assert(begins == 2 && ends == 1 && inits == 1 && grants == 1 &&
           relocations == 0 && deinitializations == 1);
    allow_grant = true;
    assert(esp_elf_relocate_privileged_verified_v1(&module, bytes, 1024,
                                                   exact, 2) == -ENOSYS);
    assert(begins == 3 && ends == 2 && inits == 2 && grants == 2 &&
           relocations == 1 && deinitializations == 2);
    puts("Privileged relocation entry: exact imports and one-shot module grant before mapping PASS");
    return 0;
}
