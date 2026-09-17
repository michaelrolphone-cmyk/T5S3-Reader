#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "private/esp_privileged_os_cpu.h"

/* Synthetic strong definitions exercise the production 46-symbol inventory. */
#define RISC_OS_CPU_SYMBOL(name) \
    __attribute__((used)) const unsigned char fake_os_symbol_##name[] __asm__(#name) = { 1 };
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL

static int task_a, task_b, provider_module, ordinary_module;
static TaskHandle_t active_task;
TaskHandle_t test_current_task(void) { return active_task; }

int main(void)
{
    active_task = &task_a;
    assert(esp_elf_privileged_os_cpu_symbol_count_v1() == 46u);
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("esp_intr_alloc") == 0u);
    assert(esp_elf_privileged_os_cpu_lookup_v1("__stack_chk_guard") == 0u);
    assert(!esp_elf_privileged_os_cpu_end_v1());
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(&provider_module));

    assert(esp_elf_privileged_os_cpu_begin_v1());
    assert(esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(!esp_elf_privileged_os_cpu_begin_v1()); /* non-reentrant scope */
    assert(esp_elf_privileged_os_cpu_lookup_v1("esp_intr_alloc") ==
           (uintptr_t)fake_os_symbol_esp_intr_alloc);
    assert(esp_elf_privileged_os_cpu_lookup_v1("__stack_chk_guard") ==
           (uintptr_t)fake_os_symbol___stack_chk_guard);
    assert(esp_elf_privileged_os_cpu_lookup_v1("xTaskGetTickCount") ==
           (uintptr_t)fake_os_symbol_xTaskGetTickCount);
    assert(esp_elf_privileged_os_cpu_lookup_v1("usb_host_install") == 0u);
    assert(esp_elf_privileged_os_cpu_lookup_v1("i2c_driver_install") == 0u);
    assert(esp_elf_privileged_os_cpu_lookup_v1("t5_usb_get_api") == 0u);
    assert(esp_elf_privileged_os_cpu_lookup_v1(NULL) == 0u);

    /* An unbound ordinary ELF on the same task cannot inherit the scope. */
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(NULL));
    active_task = &task_b;
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("esp_intr_alloc") == 0u);
    assert(!esp_elf_privileged_os_cpu_begin_v1()); /* concurrent denied */
    assert(!esp_elf_privileged_os_cpu_end_v1()); /* cannot steal scope */
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));

    active_task = &task_a;
    assert(esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(esp_elf_privileged_os_cpu_authorize_relocation_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(&ordinary_module));
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    /* Another task may load another ELF, but not relocate the scoped module
     * or release the grant, even before the privileged owner starts mapping. */
    active_task = &task_b;
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_relocation_leave_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));
    active_task = &task_a;
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    /* Reentrant loads, including the same module, are denied. */
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(!esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));
    assert(!esp_elf_privileged_os_cpu_end_v1()); /* cannot release active load */
    active_task = &task_b;
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_relocation_leave_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));
    active_task = &task_a;
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_end_v1());
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("esp_intr_alloc") == 0u);
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&provider_module));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&ordinary_module));

    active_task = &task_b;
    assert(esp_elf_privileged_os_cpu_begin_v1());
    assert(esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&ordinary_module));
    assert(esp_elf_privileged_os_cpu_end_v1());
    active_task = NULL;
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(!esp_elf_privileged_os_cpu_begin_v1());
    assert(esp_elf_privileged_os_cpu_lookup_v1("esp_intr_alloc") == 0u);
    puts("Privileged OS/CPU v1: exact imports, cross-task isolation, single-module grant, nested denial PASS");
    return 0;
}
