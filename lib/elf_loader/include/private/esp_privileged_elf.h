#pragma once
/* PRIVATE: only the firmware's trusted package loader may invoke this after
 * validating package signature, identity, ABI, and exact imported symbols.
 * The byte image is supplied by the verifier, not reopened by pathname, so a
 * file replacement between signature verification and relocation is avoided.
 * This header is NOT a public native-app/driver capability and is not exported
 * through the ELF symbol table. It does not implement package verification.
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On success, *module owns relocated code/data and must remain resident until
 * generic provider quiescence (ISR, DMA, tasks, callbacks) is established.
 * On failure, the partially mapped image is deinitialized by this routine.
 * The caller must initialize no other references to *module before success.
 * -EINVAL malformed input, -EBUSY another privileged relocation is active,
 * -EIO unexpected scope failure, or the native relocator's negative error.
 */
int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *verified_bytes,
                                            size_t verified_length);

#ifdef __cplusplus
}
#endif
