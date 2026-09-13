    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

#define HOST_OUT 0x11223344

_start:
    la sp, __stack_top

    /*
     * Consecutive CSR writes are deferred by the CPU pipeline. Every rd must
     * still receive the value that existed before its own write, including
     * when the following instruction accesses the same CSR immediately.
     */
    li t0, 0x13579bdf
    li t1, 0x2468ace0
    csrw mscratch, zero
    csrrw s0, mscratch, t0
    csrrw s1, mscratch, t1
    csrrs s2, mscratch, zero

    bnez s0, fail
    bne s1, t0, fail
    bne s2, t1, fail

    la a0, pass_text
    call puts
1:
    j 1b

fail:
    la a0, fail_text
    call puts
2:
    j 2b

puts:
    mv t2, a0
3:
    lbu t0, 0(t2)
    beqz t0, 4f
    li t1, HOST_OUT
    sw t0, 0(t1)
    addi t2, t2, 1
    j 3b
4:
    ret

    .size _start, . - _start

    .section .rodata
pass_text:
    .asciz "CSR_ORDER\n"
fail_text:
    .asciz "FAIL CSR_ORDER\n"
