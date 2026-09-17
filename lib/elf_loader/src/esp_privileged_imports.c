/* Import allowlist for the private generic OS/CPU provider loader.
 * NO device implementation or dynamic-module symbol is an approved import.
 * This is compatibility preflight, not signer authentication or a sandbox.
 */
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_imports.h"

/* Snapshot the ordinary libc contract at ABI v1. Deliberately do not call
 * elf_find_sym_default(): that resolver also searches registered/customer
 * exports and already-loaded ELFs, which must not authorize a hardware ELF.
 * Keep this list synchronized with g_esp_libc_elfsyms and the strict audits.
 */
static const char *const s_public_libc[] = {
    "strerror", "memset", "memcpy", "strlen", "strtod", "strrchr",
    "strchr", "strcmp", "strtol", "strcspn", "strncat",
    "puts", "putchar", "fputc", "fputs", "printf", "vfprintf",
    "fprintf", "fwrite", "usleep", "sleep", "exit", "close",
    "malloc", "calloc", "realloc", "free", "clock_gettime", "strftime",
    "pthread_create", "pthread_attr_init", "pthread_attr_setstacksize",
    "pthread_detach", "pthread_join", "pthread_exit", "__errno",
    "__getreent", "__locale_ctype_ptr", "_ctype_", "__ltdf2",
    "__fixunsdfsi", "__gtdf2", "__floatunsidf", "__divdf3",
    "getopt_long", "optind", "opterr", "optarg", "optopt",
    "longjmp", "setjmp"
};
static const char *const s_privileged[] = {
#define RISC_OS_CPU_SYMBOL(name) #name,
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL
};

static bool within(size_t length, uint32_t start, uint32_t size)
{
    return start <= length && size <= length - start;
}

static bool permitted(const char *name)
{
    for (size_t i = 0; i < sizeof(s_public_libc) / sizeof(s_public_libc[0]); ++i)
        if (strcmp(name, s_public_libc[i]) == 0) return true;
    for (size_t i = 0; i < sizeof(s_privileged) / sizeof(s_privileged[0]); ++i)
        if (strcmp(name, s_privileged[i]) == 0) return true;
    return false;
}

bool esp_elf_privileged_imports_valid_v1(const uint8_t *image, size_t length)
{
    /* Also check the offsets here so this helper never reads past the buffer
     * even if a future caller forgets the mandatory structural validator. */
    if (!image || length < sizeof(elf32_hdr_t) || length > 8u * 1024u * 1024u)
        return false;
    const elf32_hdr_t *header = (const elf32_hdr_t *)image;
    if (!header->shnum || header->shnum > 256 ||
        header->shentsize != sizeof(elf32_shdr_t) ||
        !within(length, header->shoff,
                (uint32_t)header->shnum * sizeof(elf32_shdr_t))) return false;
    const elf32_shdr_t *sections =
        (const elf32_shdr_t *)(image + header->shoff);
    bool saw_dynamic_symbols = false;
    for (uint32_t i = 0; i < header->shnum; ++i) {
        const elf32_shdr_t *section = &sections[i];
        if (section->type != SHT_SYNSYM) continue;
        if (saw_dynamic_symbols || section->link >= header->shnum ||
            sections[section->link].type != SHT_STRTAB ||
            section->size % sizeof(elf32_sym_t) ||
            !within(length, section->offset, section->size)) return false;
        saw_dynamic_symbols = true;
        const elf32_shdr_t *names = &sections[section->link];
        if (!within(length, names->offset, names->size)) return false;
        const elf32_sym_t *symbols =
            (const elf32_sym_t *)(image + section->offset);
        const size_t count = section->size / sizeof(elf32_sym_t);
        for (size_t j = 0; j < count; ++j) {
            const elf32_sym_t *sym = &symbols[j];
            if (sym->shndx != 0) continue; /* Only undefined imports. */
            if (j == 0 && sym->name == 0) continue; /* ELF null symbol. */
            const unsigned binding = ELF_ST_BIND(sym->info);
            if (binding != STB_GLOBAL && binding != STB_WEAK) return false;
            if (sym->name >= names->size) return false;
            const char *name = (const char *)image + names->offset + sym->name;
            const size_t remaining = names->size - sym->name;
            if (!name[0] || !memchr(name, 0, remaining < 1024 ? remaining : 1024) ||
                !permitted(name)) return false;
        }
    }
    return saw_dynamic_symbols;
}
