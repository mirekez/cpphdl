    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

#define HOST_OUT 0x11223344

_start:
    la sp, __stack_top

    call patched_instruction
    li t0, 1
    bne a0, t0, fail

    /* Replace `addi a0, zero, 1` with `addi a0, zero, 2`. */
    la t0, patched_instruction
    li t1, 0x00200513
    sw t1, 0(t0)
    fence.i

    call patched_instruction
    li t0, 2
    bne a0, t0, fail

    la a0, pass_text
    call puts
1:
    j 1b

fail:
    la a0, fail_text
    call puts
2:
    j 2b

    .balign 4
patched_instruction:
    addi a0, zero, 1
    ret

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
    .asciz "FENCE_I_ORDER\n"
fail_text:
    .asciz "FAIL FENCE_I_ORDER\n"
