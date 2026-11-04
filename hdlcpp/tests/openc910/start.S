/* Machine-mode startup for the upstream smart_run memory map. */
.section .text.start,"ax",@progbits
.globl _start
_start:
    .option push
    .option norelax
    la sp, __stack_top
    la t0, trap
    csrw mtvec, t0
    .option pop
    /* OpenC910 mxstatus / msmpr, matching smart_run's machine-mode setup. */
    li t0, 0x638000
    csrs 0x7c0, t0
    csrsi 0x7f3, 1
    /* Invalidate predictors and caches, then enable the upstream cache policy. */
    li t0, 0x70013
    csrs 0x7c2, t0
    li t0, 0x11ff
    csrs 0x7c1, t0
    li t0, 0x6e30c
    csrs 0x7c5, t0
    li t0, 0xe0000009
    csrs 0x7c3, t0
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:  call main
    bnez a0, trap
    fence rw, rw
    /* The original testbench watches these integer writeback signatures. */
    li t0, 0x444333222
    add t1, zero, t0
3:  j 3b
trap:
    li t0, 0x2382348720
    add t1, zero, t0
4:  j 4b
