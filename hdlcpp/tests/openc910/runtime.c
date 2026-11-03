#include <stdint.h>

void printstr(const char *text)
{
    /* smart_run's existing noncacheable console, with 32-bit write strobes. */
    volatile uint32_t *console = (volatile uint32_t *)(uintptr_t)0x01fffff0;
    while (*text) {
        *console = (unsigned char)*text++;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }
}
