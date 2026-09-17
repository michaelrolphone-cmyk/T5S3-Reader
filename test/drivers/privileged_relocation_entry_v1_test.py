#!/usr/bin/env python3
"""Compile and exercise the REAL esp_elf_relocate() public entry body.

The native mapping backend is mocked: this checks that every public caller,
including a same-task nested app load, must pass the module-specific guard.
The OS/CPU guard itself is compiled and tested separately in C.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
loader = (root / 'lib/elf_loader/src/esp_elf.c').read_text()
signature = 'int esp_elf_relocate(esp_elf_t *elf, const uint8_t *pbuf)\n{'
assert loader.count(signature) == 1, 'Production ELF public relocation guard missing'
start = loader.index(signature)
opening = loader.index('{', start)
depth = 0
closing = None
for pos in range(opening, len(loader)):
    if loader[pos] == '{':
        depth += 1
    elif loader[pos] == '}':
        depth -= 1
        if depth == 0:
            closing = pos + 1
            break
assert closing is not None
function = loader[start:closing]
assert 'esp_elf_privileged_os_cpu_relocation_enter_v1(elf)' in function
assert 'esp_elf_privileged_os_cpu_relocation_leave_v1(elf)' in function
assert 'esp_elf_relocate_impl(elf, pbuf)' in function

prefix = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef struct { int dummy; } esp_elf_t;
static esp_elf_t provider, ordinary;
static bool scoped, consumed, active, force_leave_failure, trigger_nested;
static int backend_calls, enters, leaves, backend_result, nested_result;
static const esp_elf_t *authorized;
int esp_elf_relocate(esp_elf_t *elf, const uint8_t *pbuf);
static bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module) {
    ++enters;
    if (scoped && (module != authorized || consumed || active)) return false;
    if (scoped) { consumed = true; active = true; }
    return true;
}
static bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module) {
    ++leaves;
    if (force_leave_failure) return false;
    if (scoped) {
        if (module != authorized || !active) return false;
        active = false;
    }
    return true;
}
static int esp_elf_relocate_impl(esp_elf_t *elf, const uint8_t *image) {
    (void)elf;
    ++backend_calls;
    if (trigger_nested) {
        trigger_nested = false;
        nested_result = esp_elf_relocate(&ordinary, image);
    }
    return backend_result;
}
'''
suffix = r'''
int main(void) {
    const uint8_t image[4] = {0};
    assert(esp_elf_relocate(NULL, image) == -EINVAL);
    assert(esp_elf_relocate(&provider, NULL) == -EINVAL);
    assert(!enters && !backend_calls && !leaves);
    assert(esp_elf_relocate(&ordinary, image) == 0);
    assert(enters == 1 && backend_calls == 1 && leaves == 1);
    scoped = true;
    assert(esp_elf_relocate(&ordinary, image) == -EPERM);
    assert(esp_elf_relocate(&provider, image) == -EPERM);
    assert(backend_calls == 1 && leaves == 1); /* never mapped */
    authorized = &provider;
    assert(esp_elf_relocate(&ordinary, image) == -EPERM);
    trigger_nested = true;
    assert(esp_elf_relocate(&provider, image) == 0);
    assert(nested_result == -EPERM);
    assert(backend_calls == 2 && leaves == 2 && !active);
    assert(esp_elf_relocate(&provider, image) == -EPERM); /* spent */
    consumed = false;
    backend_result = -ENOSYS;
    assert(esp_elf_relocate(&provider, image) == -ENOSYS);
    assert(!active); /* backend failure must still leave */
    consumed = false;
    backend_result = 0;
    force_leave_failure = true;
    assert(esp_elf_relocate(&provider, image) == -EIO);
    scoped = false;
    force_leave_failure = false;
    assert(esp_elf_relocate(&ordinary, image) == 0);
    puts("Actual ELF relocation entry: same-task nested/bystander denied, scope exit PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='riscrte-relocation-entry-') as temporary:
    src = Path(temporary) / 'entry.c'
    exe = Path(temporary) / 'entry'
    src.write_text(prefix + '\n' + function + '\n' + suffix)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall',
                    '-Wextra', '-Werror', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
