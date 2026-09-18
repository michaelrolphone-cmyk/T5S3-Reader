/* The native ELF loader allocates/maps .text as one executable region.
 * IDF ISR routines emit .iram1.N and .iram1.N.literal orphan sections;
 * leaving those as separate outputs produces RELATIVE relocations whose
 * target esp_elf_map_sym cannot resolve (-EINVAL during USB acquisition).
 * Keep their code and literal pools within the actual .text allocation.
 * INSERT retains the platform's default dynamic ELF linker script.
 */
SECTIONS {
  .text : {
    *(.text .text.* .gnu.linkonce.t.*)
    *(.iram1 .iram1.*)
  }
} INSERT BEFORE .rodata;
