    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    csrr a0, mhartid
    la sp, __stack_top
    slli t0, a0, 12
    sub sp, sp, t0

    /* The ELF loader supplies zero-filled BSS before all harts leave reset. */
    call main
1:
    j 1b

    .size _start, . - _start
