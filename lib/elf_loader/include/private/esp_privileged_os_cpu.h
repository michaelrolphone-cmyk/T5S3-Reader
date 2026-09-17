#pragma once
/* Private loader interface: never register these functions in an ELF export
 * table or expose them through an application capability. They only control
 * import resolution while trusted firmware is relocating a verified provider.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RISC_PRIVILEGED_OS_CPU_ABI_V1 1u

/* The caller MUST have verified the provider's package identity, signature,
 * declared ABI version and exact import allowlist before beginning this scope.
 * This low-level mechanism is not itself a package verifier or a sandbox.
 * Only one privileged relocation scope may exist at a time. */
bool esp_elf_privileged_os_cpu_begin_v1(void);
/* Returns false without clearing somebody else's scope (fail closed). */
bool esp_elf_privileged_os_cpu_end_v1(void);
/* Only the owning task can resolve symbols; other tasks always get zero. */
uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char *symbol);
size_t esp_elf_privileged_os_cpu_symbol_count_v1(void);

#ifdef __cplusplus
}
#endif
