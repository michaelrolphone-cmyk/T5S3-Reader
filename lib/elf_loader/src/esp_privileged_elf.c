/* Private verified-provider relocation entry point. No hardware logic here. */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"
#include "private/esp_privileged_os_cpu.h"

extern bool esp_elf_validate_file(const uint8_t *image, size_t length);

int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *verified_bytes,
                                            size_t verified_length,
                                            const char *const *signed_imports,
                                            size_t signed_import_count)
{
    /* Authentication of this SAME digest-checked snapshot and the exact
     * declaration list is the privileged Package Manager's responsibility.
     * Enforce both ABI-wide and manifest-exact import bounds BEFORE granting
     * resolution scope or mapping executable memory. A self-declared list is
     * not signer trust and must never originate from an application. */
    if (!module || !verified_bytes || !signed_imports || !signed_import_count ||
        !esp_elf_validate_file(verified_bytes, verified_length) ||
        !esp_elf_privileged_imports_valid_v1(verified_bytes, verified_length) ||
        !esp_elf_privileged_manifest_imports_match_v1(
            verified_bytes, verified_length, signed_imports, signed_import_count))
        return -EINVAL;

    /* Never swap the global resolver or globally register private imports.
     * elf_find_sym bypasses any installed custom resolver while this task
     * owns the scope and dispatches directly to the private-scoped default.
     * Other tasks retain ordinary lookup; nested privileged scopes fail.
     * This scope is task-owned rather than bound to one module. The trusted
     * executor must also prevent same-task ordinary relocation reentrancy. */
    if (!esp_elf_privileged_os_cpu_begin_v1()) return -EBUSY;
    int result = esp_elf_init(module);
    if (result == 0) {
        result = esp_elf_relocate(module, verified_bytes);
        if (result != 0) esp_elf_deinit(module);
    }
    if (!esp_elf_privileged_os_cpu_end_v1()) {
        /* A mismatched owner can never be silently cleared; the scope stays
         * closed to all other tasks. Do not return a live driver in this case. */
        if (result == 0) esp_elf_deinit(module);
        return -EIO;
    }
    return result;
}
