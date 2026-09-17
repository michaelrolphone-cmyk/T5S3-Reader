#!/usr/bin/env python3
"""One-shot SHA-pinned edit of the pinned upstream ELF loader entry.

Only replace the public relocation function's signature/opening with an exact
module-grant wrapper and rename the existing implementation to static. No
changes to upstream section/segment/relocation backend or hardware behavior.
The temporary CI write workflow and this script are removed after landing.
"""
from pathlib import Path
import hashlib

path = Path('lib/elf_loader/src/esp_elf.c')
original = path.read_bytes()
def blob_sha(data):
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
expected = '4f32b0bffc5b53bfa3fb647920a13184e75bf4bd'
if blob_sha(original) != expected:
    raise SystemExit(f'REFUSING unexpected loader blob {blob_sha(original)} != {expected}')
old = b'int esp_elf_relocate(esp_elf_t *elf, const uint8_t *pbuf)\n{'
new = b'''/* The implementation never handles scope admission. Every public caller,
 * including ordinary dlopen and trusted provider loading, passes this entry.
 * Only the verified provider module has a one-use grant while its task owns
 * privileged import resolution; nested ordinary/same-module loads are denied.
 * All return paths (including partial mapping errors) revoke active state. */
static int esp_elf_relocate_impl(esp_elf_t *elf, const uint8_t *pbuf);

int esp_elf_relocate(esp_elf_t *elf, const uint8_t *pbuf)
{
    if (!elf || !pbuf) return -EINVAL;
    if (!esp_elf_privileged_os_cpu_relocation_enter_v1(elf)) return -EPERM;
    int result = esp_elf_relocate_impl(elf, pbuf);
    if (!esp_elf_privileged_os_cpu_relocation_leave_v1(elf)) return -EIO;
    return result;
}

static int esp_elf_relocate_impl(esp_elf_t *elf, const uint8_t *pbuf)
{'''
if original.count(old) != 1 or original.count(b'esp_elf_privileged_os_cpu_relocation_enter_v1'):
    raise SystemExit('REFUSING unreviewed source structure or guard already present')
modified = original.replace(old, new, 1)
if modified.count(b'esp_elf_privileged_os_cpu_relocation_enter_v1(elf)') != 1:
    raise SystemExit('REFUSING duplicate relocation grant check')
path.write_bytes(modified)
print(f'Patched production ELF relocate entry {blob_sha(original)} -> {blob_sha(modified)}')
