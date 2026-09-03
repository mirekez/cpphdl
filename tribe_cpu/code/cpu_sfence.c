#include "uart.h"

static inline void sfence_vma(void)
{
    __asm__ volatile("sfence.vma zero, zero" : : : "memory");
}

int main(void)
{
    /*
     * SFENCE.VMA flushes address translations, not the instruction cache.
     * Repeating it makes the cache performance assertion in CPU_test sensitive
     * to an accidental coupling between the two invalidation paths.
     */
    for (unsigned i = 0; i < 16; ++i) {
        sfence_vma();
    }

    /* Exactly one explicit I-cache invalidation is expected by CPU_test. */
    __asm__ volatile("fence.i" : : : "memory");
    tribe_uart_puts("SFENCE\n");
    return 0;
}
