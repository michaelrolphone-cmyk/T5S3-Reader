# U1 mapped data-section alignment correction

## Confirmed production defect

Current production configuration enables LOAD_PSRAM, BUS_ADDRESS_MIRROR and
CACHE_OFFSET. Executable allocation therefore uses SPIRAM plus byte-accessible
memory. The separate no-PSRAM executable-copy investigation is not this change.

The production section loader nevertheless concatenated data, rodata,
data.rel.ro and BSS without padding. The Xtensa relocator accesses mapped
locations through uint32_t pointers. In the verified **014fb6f7** CI artifacts:

- Clock ELF: 29,544 bytes, SHA-256
  `a5231d3f21931261b10ae82a3e16248d60a678aa6ebc67b61cb702491d6a0ece`.
  Its 33-byte rodata placed data.rel.ro at offset33, leaving eight relocation
  destinations unaligned under the old packing.
- Archive service ELF: 115,660 bytes, SHA-256
  `58d02bfd19f6d49565e6502acb7e3d8e37abd64bdc8f310cd9fcd7b2e8e13676`.
  Fourteen of its64 relocation destinations were unaligned; data/BSS also
  declare eight-byte alignment.

These came from workflow36816333240 artifact11141119539, whose downloaded ZIP
SHA-256 is `59fac70da24a7f3c2cf51d4287b627574337a8b5db50bffa53991451eb52d946`.
This is source/actual-artifact evidence, not an observed hardware crash. The
separate74b0 full-firmware artifact was not inspected after retrieval failed.

## Shared layout rule

`private/esp_elf_data_layout.h` reserves checked padding and places each data
section using the **absolute allocation pointer**, not a presumed heap
alignment. Declared alignment must be zero/one or a power of two consistent
with the virtual address. Physical addresses preserve the virtual residue
modulo at least four bytes, so aligned virtual relocation sites remain aligned.

Padding changes only physical allocation: exact virtual addresses and section
sizes remain unchanged. Original pdata remains the allocation/free pointer;
per-section addresses identify interior aligned data. Empty sections consume
no padding. The existing eight-MiB image bound now includes physical padding.
Duplicate mapped sections, overlapping virtual ranges, arithmetic overflow and
invalid alignment fail before allocation. Failed data allocation/layout/MMU
setup frees owned buffers and clears their pointers/section addresses before
caller cleanup. The Xtensa relocation writer independently refuses an unaligned
mapped destination before dereferencing it.

The executable allocation/copy and cache-alias contracts are retained. This
helper is not a new loader or a replacement for file/ABI/import preflight.

## Verification

`elf_section_layout_test.py` compiles the actual production section-loader,
address mapper and Xtensa relocation bodies. Linux low-address allocations
stand in for byte-accessible heaps; target cache-alias translation is disabled
and no guest entrypoint runs. The same harness
fails an alignment assertion with the preceding production loader.

The corrected loader passes the synthetic mixed-alignment fixture and both
exact ELFs above across16 allocation-address residues, all15 clock and64 archive
relocation sites, byte-for-byte text/data/rodata checks, zero BSS, allocation
guards, overflow, huge padding, duplicates, overlap and malformed alignment.
Forced first/second allocation and MMU-init failures leave no double-freeable
pointers. UndefinedBehaviorSanitizer runs with fatal diagnostics.

The native aggregate runs the synthetic fixture once; the existing USB/ELF CI
also runs it on that build's actual clock and archive ELFs. Current local
verification is separate from the forthcoming exact-head target checks.

**Implementation In Progress**
