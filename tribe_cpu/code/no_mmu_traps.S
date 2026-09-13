// Synchronous traps must work with both MMU and interrupt routing disabled.
#ifndef TEST_UART_BASE
#define TEST_UART_BASE 0x70000
#endif
.section .text
.global _start
_start:
    la t0, trap_handler
    csrw mtvec, t0
    li s0, 0
    li s1, 11
    la s2, machine_ecall
machine_ecall:
    ecall
    li t0, 1
    bne s0, t0, fail
    li t0, 0x1800
    csrc mstatus, t0
    la t0, user_start
    csrw mepc, t0
    mret
user_start:
    li s1, 8
    la s2, user_ecall
user_ecall:
    ecall
    li s1, 3
    la s2, user_break
user_break:
    ebreak
    li s1, 2
    la s2, user_illegal
user_illegal:
    csrr t0, mstatus // Machine CSR access is illegal in U mode.
    li t0, 4
    bne s0, t0, fail
    la a0, success
    j print
.balign 4
trap_handler:
    csrr t0, mcause
    bne t0, s1, fail
    csrr t0, mepc
    bne t0, s2, fail
    addi s0, s0, 1
    addi t0, t0, 4
    csrw mepc, t0
    mret
fail:
    la a0, failure
print:
    li t0, TEST_UART_BASE
1:
    lbu t1, 0(a0)
    beqz t1, stop
    sw t1, 0(t0)
    addi a0, a0, 1
    j 1b
stop:
    j stop
.section .rodata
success: .asciz "NO_MMU_TRAPS_PASS\n"
failure: .asciz "NO_MMU_TRAPS_FAIL\n"
