OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
    code (rx) : ORIGIN = 0, LENGTH = 0x40000
    data (rw) : ORIGIN = 0x40000, LENGTH = 0xc0000
}
__stack_top = 0xee000;
SECTIONS {
    .text : { *(.text.start) *(.text*) } > code
    .rodata : { *(.rodata*) *(.srodata*) } > code
    .data : { *(.data*) *(.sdata*) } > data
    .bss (NOLOAD) : ALIGN(64) {
        __bss_start = .;
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > data
}
