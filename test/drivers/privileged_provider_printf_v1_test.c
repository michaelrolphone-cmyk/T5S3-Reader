/* Verify only a privileged provider's relocation scope substitutes printf
 * and compiler-optimized puts. Ordinary applications cannot inherit the
 * diagnostics sink or any USB hardware implementation from the resolver. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/task.h"
#include "private/esp_privileged_os_cpu.h"

#define RISC_OS_CPU_SYMBOL(name) \
    __attribute__((used)) const unsigned char fake_os_symbol_##name[] __asm__(#name) = { 1 };
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL

static int owner, bystander;
static TaskHandle_t active_task;
TaskHandle_t test_current_task(void) { return active_task; }

int risc_provider_diagnostic_printf(const char *format, ...) {
    (void)format;
    return 0;
}
int risc_provider_diagnostic_puts(const char *message) {
    (void)message;
    return 0;
}

int main(void) {
    active_task = &owner;
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    assert(esp_elf_privileged_os_cpu_lookup_v1("puts") == 0);
    assert(esp_elf_privileged_os_cpu_begin_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") ==
           (uintptr_t)&risc_provider_diagnostic_printf);
    assert(esp_elf_privileged_os_cpu_lookup_v1("puts") ==
           (uintptr_t)&risc_provider_diagnostic_puts);
    assert(esp_elf_privileged_os_cpu_lookup_v1("t5_usb_get_api") == 0);
    active_task = &bystander;
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    assert(esp_elf_privileged_os_cpu_lookup_v1("puts") == 0);
    active_task = &owner;
    assert(esp_elf_privileged_os_cpu_end_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    assert(esp_elf_privileged_os_cpu_lookup_v1("puts") == 0);
    puts("Privileged provider printf/puts diagnostics remain task-scoped: PASS");
    return 0;
}
