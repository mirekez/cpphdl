#include "uart.h"

static unsigned char src[256] __attribute__((aligned(32)));
static unsigned char dst[256] __attribute__((aligned(32)));

static void noinline_wordcopy_unaligned(unsigned char* d, const unsigned char* s) __attribute__((noinline));
static void noinline_wordcopy_unaligned(unsigned char* d, const unsigned char* s)
{
    __asm__ volatile(
        "lw t0, 0(%1)\n"
        "lw t1, 4(%1)\n"
        "lw t2, 8(%1)\n"
        "lw t3, 12(%1)\n"
        "lw t4, 16(%1)\n"
        "lw t5, 20(%1)\n"
        "lw t6, 24(%1)\n"
        "sw t0, 0(%0)\n"
        "sw t1, 4(%0)\n"
        "sw t2, 8(%0)\n"
        "sw t3, 12(%0)\n"
        "sw t4, 16(%0)\n"
        "sw t5, 20(%0)\n"
        "sw t6, 24(%0)\n"
        :
        : "r"(d), "r"(s)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
}

static void put_u8_hex(unsigned char value)
{
    const char* hex = "0123456789abcdef";
    tribe_uart_putc(hex[value >> 4]);
    tribe_uart_putc(hex[value & 15]);
}

int main(void)
{
    int i;
    unsigned char* s;
    unsigned char* d;

    for (i = 0; i < 256; ++i) {
        src[i] = (unsigned char)(i * 37 + 11);
        dst[i] = 0;
    }

    s = src + 67;
    d = dst + 1;
    noinline_wordcopy_unaligned(d, s);

    for (i = 0; i < 28; ++i) {
        if (d[i] != s[i]) {
            tribe_uart_puts("UNALIGNED_WORDCOPY_MISMATCH ");
            tribe_uart_put_i32(i);
            tribe_uart_putc(' ');
            put_u8_hex(d[i]);
            tribe_uart_putc(' ');
            put_u8_hex(s[i]);
            tribe_uart_putc('\n');
            tribe_uart_puts("UNALIGNED_WORDCOPY_FAIL\n");
            for (;;) {
            }
        }
    }

    tribe_uart_puts("UNALIGNED_WORDCOPY\n");
    for (;;) {
    }
}
