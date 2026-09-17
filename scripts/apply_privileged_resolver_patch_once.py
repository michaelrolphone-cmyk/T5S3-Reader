#!/usr/bin/env python3
"""One-time, SHA-pinned source edit for the privileged ELF resolver.

This script modifies only the exact reviewed esp_elf.c blob. The separately
scoped CI workflow is temporary and will be deleted after the patch lands.
"""
from pathlib import Path
import hashlib

path = Path('lib/elf_loader/src/esp_elf.c')
source = path.read_bytes()
blob_sha = hashlib.sha1(b'blob ' + str(len(source)).encode() + b'\0' + source).hexdigest()
expected_sha = 'c80f89b550fcba8c346d7c9b6f724bda3cfa5693'
if blob_sha != expected_sha:
    raise SystemExit(f'REFUSING unreviewed esp_elf.c blob: {blob_sha} != {expected_sha}')
old_include = b'#include "private/elf_platform.h"\n'
new_include = old_include + b'#include "private/esp_privileged_os_cpu.h"\n'
old_lookup = b'''    taskENTER_CRITICAL(&resolver_mux);
    symbol_resolver resolver = current_resolver;
    taskEXIT_CRITICAL(&resolver_mux);
    return resolver(sym_name);
'''
new_lookup = b'''    /* Privileged relocation must never invoke a process-global custom
     * resolver, regardless of when another task installs or replaces it.
     * The default lookup is task-scoped and returns ONLY the fixed OS/CPU
     * inventory and libc while this task owns the privileged scope. Other
     * tasks retain the ordinary application's customizable namespace.
     * Do not modify current_resolver or hold resolver_mux across callbacks. */
    if (esp_elf_privileged_os_cpu_scope_owned_v1()) {
        return elf_find_sym_default(sym_name);
    }

    taskENTER_CRITICAL(&resolver_mux);
    symbol_resolver resolver = current_resolver;
    taskEXIT_CRITICAL(&resolver_mux);
    return resolver(sym_name);
'''
if source.count(old_include) != 1 or source.count(old_lookup) != 1:
    raise SystemExit('REFUSING: source structure does not match reviewed patch')
modified = source.replace(old_include, new_include, 1).replace(old_lookup, new_lookup, 1)
if modified.count(b'esp_elf_privileged_os_cpu_scope_owned_v1()') != 1:
    raise SystemExit('REFUSING: unexpected privileged scope sites in loader')
path.write_bytes(modified)
print(f'Patched {path}: {blob_sha} -> ' + hashlib.sha1(
    b'blob ' + str(len(modified)).encode() + b'\0' + modified).hexdigest())
