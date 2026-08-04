#include <stdint.h>

#define UART_BASE 0x70000u
#define UART_THR  0x00u
#define UART_LSR  0x05u
#define UART_LSR_THRE 0x20u

static inline void cpu_fence_ioreadwrite(void)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
}

static void uart_putc_fenced(char ch)
{
    while (((*(volatile uint8_t*)(UART_BASE + UART_LSR)) & UART_LSR_THRE) == 0u) {
        cpu_fence_ioreadwrite();
    }
    cpu_fence_ioreadwrite();
    *(volatile uint8_t*)(UART_BASE + UART_THR) = (uint8_t)ch;
    cpu_fence_ioreadwrite();
}

int main(void)
{
    const char* text = "FENCE\n";
    while (*text) {
        uart_putc_fenced(*text++);
    }
    return 0;
}
