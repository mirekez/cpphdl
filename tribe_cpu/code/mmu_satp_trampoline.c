#include <stdint.h>

/*
 * A stackless Sv32 smoke test.  The page tables live well above this tiny
 * image, identity-map the code page and UART page, and then execution crosses
 * the SATP write before emitting the completion marker.
 */
__attribute__((naked, noreturn, section(".text.start")))
void _start(void)
{
    __asm__ volatile(
        "li t0, 0x10000\n"          /* root page table */
        "li t1, 0x11000\n"          /* level-0 page table */
        "li t2, 0x4401\n"           /* PPN 0x11, V */
        "sw t2, 0(t0)\n"

        /* Identity-map VA 0x00000000 with R/W/X/A/D permissions. */
        "li t2, 0x0cf\n"
        "sw t2, 0(t1)\n"

        /* Identity-map the UART page at 0x00070000. */
        "li t2, 0x1c0cf\n"
        "sw t2, 0x1c0(t1)\n"        /* VPN[0] 0x70 * 4 */

        "li t0, 0x80000010\n"       /* MODE=Sv32, root PPN=0x10 */
        "csrw satp, t0\n"
        "sfence.vma zero, zero\n"

        "li t0, 0x70000\n"
        "li t1, 'M'\n" "sw t1, 0(t0)\n"
        "li t1, 'M'\n" "sw t1, 0(t0)\n"
        "li t1, 'U'\n" "sw t1, 0(t0)\n"
        "li t1, '_'\n" "sw t1, 0(t0)\n"
        "li t1, 'S'\n" "sw t1, 0(t0)\n"
        "li t1, 'A'\n" "sw t1, 0(t0)\n"
        "li t1, 'T'\n" "sw t1, 0(t0)\n"
        "li t1, 'P'\n" "sw t1, 0(t0)\n"
        "li t1, 10\n"  "sw t1, 0(t0)\n"
        "1: j 1b\n"
        : : : "memory");
}
