#!/usr/bin/env python3
"""Compile the production elf_find_sym() body with mocked RTOS callbacks.

No synthetic ELF is executed. Extracting the actual function avoids maintaining
an unrelated replica and tests whether a custom resolver can still redirect
privileged lookups or whether concurrent ordinary lookup gets changed.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
loader = (root / 'lib/elf_loader/src/esp_elf.c').read_text()
needle = 'uintptr_t elf_find_sym(const char *sym_name)\n{'
assert loader.count(needle) == 1, 'Production resolver signature changed'
start = loader.index(needle)
opening = loader.index('{', start)
depth = 0
closing = None
for index in range(opening, len(loader)):
    if loader[index] == '{':
        depth += 1
    elif loader[index] == '}':
        depth -= 1
        if depth == 0:
            closing = index + 1
            break
assert closing is not None
function = loader[start:closing]
assert 'esp_elf_privileged_os_cpu_scope_owned_v1()' in function
assert 'return elf_find_sym_default(sym_name);' in function

prefix = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t (*symbol_resolver)(const char *);
typedef int portMUX_TYPE;
static portMUX_TYPE resolver_mux;
static const char *TAG = "ELF";
static bool scope_owned;
static int entered, exited, custom_calls, default_calls;
#define ESP_LOGE(tag, ...) ((void)(tag))
#define taskENTER_CRITICAL(mux) do { (void)(mux); ++entered; } while (0)
#define taskEXIT_CRITICAL(mux) do { (void)(mux); ++exited; } while (0)
static bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return scope_owned; }
static uintptr_t elf_find_sym_default(const char *symbol) {
    ++default_calls;
    if (scope_owned && strcmp(symbol, "esp_intr_alloc") == 0) return 0x1001u;
    if (strcmp(symbol, "malloc") == 0) return 0x2002u;
    return 0;
}
static uintptr_t custom_one(const char *symbol) {
    ++custom_calls;
    if (strcmp(symbol, "usb_host_install") == 0) return 0xdeadbeefu;
    if (strcmp(symbol, "malloc") == 0) return 0xcafeu;
    return 0;
}
static uintptr_t custom_two(const char *symbol) {
    ++custom_calls;
    if (strcmp(symbol, "usb_host_install") == 0) return 0xbadc0deu;
    return 0;
}
static symbol_resolver current_resolver = custom_one;
'''
suffix = r'''
int main(void) {
    assert(elf_find_sym(NULL) == 0);
    assert(!entered && !exited && !custom_calls && !default_calls);
    /* Normal loader continues to honor custom resolver. */
    assert(elf_find_sym("usb_host_install") == 0xdeadbeefu);
    assert(elf_find_sym("malloc") == 0xcafeu);
    assert(custom_calls == 2 && default_calls == 0);
    assert(entered == 2 && exited == 2);
    /* Privileged owner bypasses the installed global resolver entirely. */
    scope_owned = true;
    assert(elf_find_sym("usb_host_install") == 0);
    assert(elf_find_sym("esp_intr_alloc") == 0x1001u);
    assert(elf_find_sym("malloc") == 0x2002u);
    assert(custom_calls == 2 && default_calls == 3);
    assert(entered == 2 && exited == 2);
    /* A simultaneous global custom-resolver change must not bypass scope. */
    current_resolver = custom_two;
    assert(elf_find_sym("usb_host_install") == 0);
    assert(custom_calls == 2 && default_calls == 4);
    /* A different task does not own scope; regular resolution is unchanged. */
    scope_owned = false;
    assert(elf_find_sym("usb_host_install") == 0xbadc0deu);
    assert(custom_calls == 3 && default_calls == 4);
    assert(entered == 3 && exited == 3);
    puts("Actual ELF resolver: custom bypass rejected, ordinary tasks unchanged PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='riscrte-resolver-') as temporary:
    source = Path(temporary) / 'resolver_test.c'
    binary = Path(temporary) / 'resolver_test'
    source.write_text(prefix + '\n' + function + '\n' + suffix)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall',
                    '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
