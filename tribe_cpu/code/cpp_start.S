    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, __stack_top

    call __tribe_init_memory
    call __libc_init_array
    call main
    call exit

1:
    j 1b

    .size _start, . - _start
