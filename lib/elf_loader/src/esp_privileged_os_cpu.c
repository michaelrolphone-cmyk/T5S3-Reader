/* Generic privileged kernel/CPU ABI for independently built hardware ELFs.
 * This maps exact, versioned compatibility symbols to existing RTOS/IDF port
 * primitives. No bus, controller, device, or transport is implemented here.
 * A native ELF is not memory-isolated: the scope restricts loader binding,
 * not execution-time address access. Verified admission remains mandatory.
 */
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "private/esp_privileged_os_cpu.h"

/* Opaque assembler aliases preserve the exact ABI symbol spelling while
 * avoiding incompatible redeclarations of IDF functions and data objects.
 * Strong references intentionally make a missing port primitive a FIRMWARE
 * LINK FAILURE, never a weak/NULL export masquerading as ABI compatibility.
 * The table only takes addresses; it never calls a driver on firmware's behalf.
 */
#define RISC_OS_CPU_SYMBOL(name) \
    extern const unsigned char risc_os_cpu_link_##name[] __asm__(#name);
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL

typedef struct {
    const char *name;
    const void *address;
} risc_os_cpu_symbol_v1;

static const risc_os_cpu_symbol_v1 s_privileged_symbols_v1[] = {
#define RISC_OS_CPU_SYMBOL(name) { #name, risc_os_cpu_link_##name },
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL
};

/* A task-specific scope avoids the security bug introduced by temporarily
 * swapping the ELF loader's global resolver or registering kernel symbols in
 * esp_elf_register_symbol(). Concurrent ordinary ELF relocations remain in
 * their ordinary, unprivileged resolution namespace. Reentrant and concurrent
 * privileged scopes are rejected rather than accidentally sharing authority.
 */
static portMUX_TYPE s_scope_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_scope_owner = NULL;

bool esp_elf_privileged_os_cpu_begin_v1(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (caller == NULL) return false;
    bool acquired = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (s_scope_owner == NULL) {
        s_scope_owner = caller;
        acquired = true;
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return acquired;
}

bool esp_elf_privileged_os_cpu_end_v1(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool released = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && s_scope_owner == caller) {
        s_scope_owner = NULL;
        released = true;
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return released;
}

uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char *symbol)
{
    if (symbol == NULL || symbol[0] == '\0') return 0;
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (caller == NULL) return 0;
    taskENTER_CRITICAL(&s_scope_lock);
    const bool permitted = s_scope_owner == caller;
    taskEXIT_CRITICAL(&s_scope_lock);
    if (!permitted) return 0;

    for (size_t i = 0; i < sizeof(s_privileged_symbols_v1) /
                           sizeof(s_privileged_symbols_v1[0]); ++i) {
        if (strcmp(symbol, s_privileged_symbols_v1[i].name) == 0)
            return (uintptr_t)s_privileged_symbols_v1[i].address;
    }
    return 0;
}

size_t esp_elf_privileged_os_cpu_symbol_count_v1(void)
{
    return sizeof(s_privileged_symbols_v1) / sizeof(s_privileged_symbols_v1[0]);
}
