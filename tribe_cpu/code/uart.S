    .section .text
    .globl _start

_start:
    li t0, 0x70000
    li t1, 'U'
    sw t1, 0(t0)
    li t1, 'A'
    sw t1, 0(t0)
    li t1, 'R'
    sw t1, 0(t0)
    li t1, 'T'
    sw t1, 0(t0)
    li t1, '\n'
    sw t1, 0(t0)

1:
    j 1b
