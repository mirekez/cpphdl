    .section .text.start, "ax", @progbits
    .globl _start
    .type _start, @function

#define UART_BASE 0x70000
#define UART_LSR  5
#define UART_THR  0
#define UART_LSR_THRE 0x20

_start:
    la sp, __stack_top
    la t0, trap_entry
    csrw stvec, t0

    /*
     * Scenario: preserve x1/ra through a kernel-style trap frame.
     * BusyBox got stuck after a page fault because trap return restored
     * ra as the current helper address. This sets ra to a sentinel target,
     * takes a trap, restores the frame, then executes ret after sret.
     */
    la ra, expected_return
    ecall
after_trap:
    ret

bad_return:
    la a0, fail_text
    call puts
1:
    j 1b

expected_return:
    la a0, pass_text
    call puts
2:
    j 2b

trap_entry:
    addi sp, sp, -144
    sw ra, 4(sp)
    sw gp, 12(sp)
    sw tp, 16(sp)
    sw t0, 20(sp)
    sw t1, 24(sp)
    sw t2, 28(sp)
    sw s0, 32(sp)
    sw s1, 36(sp)
    sw a0, 40(sp)
    sw a1, 44(sp)
    sw a2, 48(sp)
    sw a3, 52(sp)
    sw a4, 56(sp)
    sw a5, 60(sp)
    sw a6, 64(sp)
    sw a7, 68(sp)
    sw s2, 72(sp)
    sw s3, 76(sp)
    sw s4, 80(sp)
    sw s5, 84(sp)
    sw s6, 88(sp)
    sw s7, 92(sp)
    sw s8, 96(sp)
    sw s9, 100(sp)
    sw s10, 104(sp)
    sw s11, 108(sp)
    sw t3, 112(sp)
    sw t4, 116(sp)
    sw t5, 120(sp)
    sw t6, 124(sp)

    csrr t0, scause
    li t1, 9
    bne t0, t1, bad_trap

    la t0, after_trap
    csrw sepc, t0

    lw ra, 4(sp)
    lw gp, 12(sp)
    lw tp, 16(sp)
    lw t0, 20(sp)
    lw t1, 24(sp)
    lw t2, 28(sp)
    lw s0, 32(sp)
    lw s1, 36(sp)
    lw a0, 40(sp)
    lw a1, 44(sp)
    lw a2, 48(sp)
    lw a3, 52(sp)
    lw a4, 56(sp)
    lw a5, 60(sp)
    lw a6, 64(sp)
    lw a7, 68(sp)
    lw s2, 72(sp)
    lw s3, 76(sp)
    lw s4, 80(sp)
    lw s5, 84(sp)
    lw s6, 88(sp)
    lw s7, 92(sp)
    lw s8, 96(sp)
    lw s9, 100(sp)
    lw s10, 104(sp)
    lw s11, 108(sp)
    lw t3, 112(sp)
    lw t4, 116(sp)
    lw t5, 120(sp)
    lw t6, 124(sp)
    addi sp, sp, 144
    sret

bad_trap:
    addi sp, sp, 144
    j bad_return

puts:
    mv t2, a0
3:
    lbu t0, 0(t2)
    beqz t0, 5f
4:
    li t1, UART_BASE + UART_LSR
    lbu t1, 0(t1)
    andi t1, t1, UART_LSR_THRE
    beqz t1, 4b
    li t1, UART_BASE + UART_THR
    sb t0, 0(t1)
    addi t2, t2, 1
    j 3b
5:
    ret

    .section .rodata
pass_text:
    .asciz "TRAP_RA\n"
fail_text:
    .asciz "FAIL TRAP_RA\n"
