/* Keep provider code/data in sections the existing loader actually maps.
 * L32R literals precede all matching IRAM code; no loader policy exception. */
SECTIONS {
  .text : {
    *(.got.plt* .plt*)
    *(.literal .literal.* .gnu.linkonce.literal.*)
    *(.iram1.literal .iram1.*.literal)
    *(.text .text.* .stub .gnu.linkonce.t.*)
    *(.iram1 .iram1.*)
  }
} INSERT BEFORE .rodata;
SECTIONS {
  .data : { *(.data .data.* .dram1 .dram1.*) }
} INSERT BEFORE .bss;
