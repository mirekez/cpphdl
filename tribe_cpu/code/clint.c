#include "clint.h"
#include "uart.h"

void _start(void)
{
    tribe_clint_disable_timer();
    tribe_clint_write32(TRIBE_CLINT_MSIP, 1u);
    tribe_clint_write32(TRIBE_CLINT_MSIP, 0u);
    tribe_clint_set_mtimecmp(64u);
    tribe_uart_puts("CLINT\n");
    for (;;) {}
}
