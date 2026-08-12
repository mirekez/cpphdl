#include "clint.h"
#include "uart.h"

#ifndef CPU_TIME_CSR_MAX_DELTA
#define CPU_TIME_CSR_MAX_DELTA 64u
#endif

static uint64_t read_time_csr(void)
{
    uint32_t hi0;
    uint32_t lo;
    uint32_t hi1;
    do {
        __asm__ volatile("rdtimeh %0" : "=r"(hi0));
        __asm__ volatile("rdtime %0" : "=r"(lo));
        __asm__ volatile("rdtimeh %0" : "=r"(hi1));
    } while (hi0 != hi1);
    return ((uint64_t)hi0 << 32) | lo;
}

int main(void)
{
    uint64_t mmio_time;
    uint64_t csr_time;
    uint32_t delta;

    tribe_clint_disable_timer();
    tribe_clint_write32(TRIBE_CLINT_MTIME_HI, 0x00123456u);
    tribe_clint_write32(TRIBE_CLINT_MTIME_LO, 0x789abcdeu);

    mmio_time = tribe_clint_mtime();
    csr_time = read_time_csr();
    delta = (uint32_t)csr_time - (uint32_t)mmio_time;
    if ((uint32_t)(csr_time >> 32) != (uint32_t)(mmio_time >> 32) ||
        (uint32_t)csr_time < (uint32_t)mmio_time ||
        delta > CPU_TIME_CSR_MAX_DELTA) {
        tribe_uart_puts("TIMECSR FAIL delta=");
        tribe_uart_put_i32((int32_t)delta);
        tribe_uart_putc('\n');
        return 1;
    }

    tribe_uart_puts("TIMECSR\n");
    return 0;
}
