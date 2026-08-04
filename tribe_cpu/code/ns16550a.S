    .section .text
    .globl _start

_start:
    li t0, 0x70000

    li t1, 0x03
    sb t1, 3(t0)      # LCR: 8 data bits, no parity, 1 stop bit

    li t2, 'N'
    jal ra, putc
    li t2, 'S'
    jal ra, putc
    li t2, '1'
    jal ra, putc
    li t2, '6'
    jal ra, putc
    li t2, '5'
    jal ra, putc
    li t2, '5'
    jal ra, putc
    li t2, '0'
    jal ra, putc
    li t2, 'A'
    jal ra, putc
    li t2, '\n'
    jal ra, putc

1:
    j 1b

putc:
    lbu t1, 5(t0)     # LSR
    andi t1, t1, 0x20 # THRE
    beqz t1, putc
    sb t2, 0(t0)      # THR
    ret
