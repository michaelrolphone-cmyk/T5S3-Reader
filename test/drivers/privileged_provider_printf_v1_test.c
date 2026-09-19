/* Verify only the privileged provider's relocation scope substitutes printf.
 * Real physical USB and power ELFs use the existing libc printf import; the
 * ordinary app resolver must never inherit this diagnostic endpoint. */
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

int main(void) {
    active_task = &owner;
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    assert(esp_elf_privileged_os_cpu_begin_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") ==
           (uintptr_t)&risc_provider_diagnostic_printf);
    assert(esp_elf_privileged_os_cpu_lookup_v1("t5_usb_get_api") == 0);
    active_task = &bystander;
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    active_task = &owner;
    assert(esp_elf_privileged_os_cpu_end_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("printf") == 0);
    puts("Privileged provider printf diagnostics remain task-scoped: PASS");
    return 0;
}
