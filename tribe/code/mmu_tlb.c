#include "uart.h"
#include <stdint.h>

static inline void write_satp(uint32_t value)
{
    __asm__ volatile("csrw satp, %0" : : "r"(value) : "memory");
}

static inline uint32_t read_satp(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, satp" : "=r"(value));
    return value;
}

static inline void sfence_vma(void)
{
    __asm__ volatile(".word 0x12000073" : : : "memory");
}

void _start(void)
{
    write_satp(0);
    sfence_vma();

    if (read_satp() != 0) {
        tribe_uart_puts("FAIL MMU_TLB\n");
        for (;;) {}
    }

    tribe_uart_puts("MMU_TLB\n");
    for (;;) {}
}
