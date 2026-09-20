#ifndef TEST_UART_BASE
#define TEST_UART_BASE 0x70000
#endif

// A completed load must retain its value while younger MUL/DIV holds execute.
.section .text
.global _start
_start:
    bnez a0, park
    la s1, data
    li a0, 0
    li a1, 582
    li s9, 0
    lw s8, 8(s1)
    lw s9, 0(s1)
    mul s10, a0, s8
    lw a0, 0(s9)
    mulhu a2, a1, s8
    add s10, a2, s10
    mul s11, a1, s8
    la t0, data
    bne s9, t0, fail
    li t0, 595968
    bne s11, t0, fail
    // Reuse each load instruction with different data, exercising fresh responses
    // after the preceding owner has retired. The independent multicycle operation
    // keeps each completed load in writeback longer than its response-valid token.
    la s1, values
    li s2, 2
next_value:
    lw s3, 0(s1)
    li a1, 21
    li a2, 7
    lb s9, 0(s1)
    mul s10, a1, a2
    lb t0, 0(s1)
    bne s9, t0, fail
    li t0, 147
    bne s10, t0, fail
    lbu s9, 1(s1)
    divu s10, a1, a2
    lbu t0, 1(s1)
    bne s9, t0, fail
    li t0, 3
    bne s10, t0, fail
    lh s9, 0(s1)
    mul s10, a1, a2
    lh t0, 0(s1)
    bne s9, t0, fail
    li t0, 147
    bne s10, t0, fail
    lhu s9, 2(s1)
    divu s10, a1, a2
    lhu t0, 2(s1)
    bne s9, t0, fail
    li t0, 3
    bne s10, t0, fail
    lw s9, 0(s1)
    mul s10, a1, a2
    lw t0, 0(s1)
    bne s9, t0, fail
    li t0, 147
    bne s10, t0, fail
    lw s9, 0(s1)
    divu s10, a1, a2
    lw t0, 0(s1)
    bne s9, t0, fail
    li t0, 3
    bne s10, t0, fail
    // A dependent multicycle operation must also receive the loaded operand.
    lw s9, 0(s1)
    mul s10, s9, a2
    mul t0, s3, a2
    bne s10, t0, fail
    bne s9, s3, fail
    addi s1, s1, 4
    addi s2, s2, -1
    bnez s2, next_value
    la a0, success
    j print
fail:
    la a0, failure
print:
    li t0, TEST_UART_BASE
1:
    lbu t1,0(a0)
    beqz t1,park
    sw t1,0(t0)
    addi a0,a0,1
    j 1b
park:
    wfi
    j park
.section .rodata
success: .asciz "LOAD_RETIRE_PASS\n"
failure: .asciz "LOAD_RETIRE_FAIL\n"
.section .data
.align 6
data: .word data,0,1024

values: .word 0x87654381, 0x123456fe
