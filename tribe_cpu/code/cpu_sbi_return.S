    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

#define HOST_OUT 0x11223344
#define SBI_SUCCESS 0
#define SBI_EXT_BASE 0x10
#define SBI_EXT_TIME 0x54494d45
#define SBI_EXT_RFENCE 0x52464e43

_start:
    la sp, __stack_top

    /*
     * Scenario: local SBI emulation returns a pair in a0/a1. The instruction
     * immediately after ecall must see the new a1 value, otherwise Linux v6.19
     * misdetects SBI v0.2 as v0.1 and loses early console progress.
     */
    li a7, SBI_EXT_BASE
    li a6, 0              /* SBI_BASE_GET_SPEC_VERSION */
    ecall
    mv s0, a0
    mv s1, a1
    li t0, SBI_SUCCESS
    bne s0, t0, fail
    li t0, 2              /* v0.2 */
    bne s1, t0, fail

    li a7, SBI_EXT_BASE
    li a6, 3              /* SBI_BASE_PROBE_EXTENSION */
    li a0, SBI_EXT_TIME
    ecall
    mv s0, a0
    mv s1, a1
    li t0, SBI_SUCCESS
    bne s0, t0, fail
    li t0, 1
    bne s1, t0, fail

    li a7, SBI_EXT_BASE
    li a6, 3              /* SBI_BASE_PROBE_EXTENSION */
    li a0, 0x735049       /* IPI, not implemented here */
    ecall
    mv s0, a0
    mv s1, a1
    li t0, SBI_SUCCESS
    bne s0, t0, fail
    bnez s1, fail

    li a7, SBI_EXT_BASE
    li a6, 3              /* SBI_BASE_PROBE_EXTENSION */
    li a0, SBI_EXT_RFENCE
    ecall
    mv s0, a0
    mv s1, a1
    li t0, SBI_SUCCESS
    bne s0, t0, fail
    li t0, 1
    bne s1, t0, fail

    li a7, SBI_EXT_RFENCE
    li a6, 1              /* SBI_EXT_RFENCE_REMOTE_SFENCE_VMA */
    li a0, 0              /* hart mask */
    li a1, 0              /* hart mask base */
    li a2, 0              /* start addr */
    li a3, 0              /* size */
    ecall
    mv s0, a0
    li t0, SBI_SUCCESS
    bne s0, t0, fail

    li a7, SBI_EXT_TIME
    li a6, 0              /* SBI_SET_TIMER */
    li a0, -1
    li a1, -1
    ecall
    mv s0, a0
    li t0, SBI_SUCCESS
    bne s0, t0, fail

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
    beqz t0, 5f
    li t1, HOST_OUT
    sw t0, 0(t1)
    addi t2, t2, 1
    j 3b
5:
    ret

    .size _start, . - _start

    .section .rodata
pass_text:
    .asciz "SBI\n"
fail_text:
    .asciz "FAIL SBI\n"
