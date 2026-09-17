/* Private verified-provider relocation entry point. No hardware logic here. */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_os_cpu.h"

extern bool esp_elf_validate_file(const uint8_t *image, size_t length);

int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *verified_bytes,
                                            size_t verified_length)
{
    /* Authentication of this SAME immutable byte buffer is still the trusted
     * installer's responsibility. Even an admitted package may not import an
     * arbitrary previously loaded ELF or firmware hardware implementation.
     * Reject unexpected imports BEFORE granting any privileged resolver scope
     * or mapping executable memory. */
    if (!module || !verified_bytes ||
        !esp_elf_validate_file(verified_bytes, verified_length) ||
        !esp_elf_privileged_imports_valid_v1(verified_bytes, verified_length))
        return -EINVAL;

    /* Never swap the process-global resolver or register privileged imports
     * globally. The default resolver consults the scoped table only for this
     * FreeRTOS task while esp_elf_relocate executes synchronously. Concurrent
     * regular app loads cannot see it. Reentrant privileged loads fail closed.
     */
    if (!esp_elf_privileged_os_cpu_begin_v1()) return -EBUSY;
    int result = esp_elf_init(module);
    if (result == 0) {
        result = esp_elf_relocate(module, verified_bytes);
        if (result != 0) esp_elf_deinit(module);
    }
    if (!esp_elf_privileged_os_cpu_end_v1()) {
        /* A mismatched owner can never be silently cleared; the scope stays
         * closed to all other tasks. Do not return a live driver in this case.
         */
        if (result == 0) esp_elf_deinit(module);
        return -EIO;
    }
    return result;
}
