#include "uart.h"

static char src[64];
static char dst[64];

static void noinline_bytecopy(char* d, const char* s, int n) __attribute__((noinline,optimize("O0")));
static void noinline_bytecopy(char* d, const char* s, int n)
{
    for (int i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

int main(void)
{
    const char* text = "MEMCPY_BYTE\n";
    int i = 0;

    while (text[i]) {
        src[i] = text[i];
        ++i;
    }
    src[i] = 0;

    noinline_bytecopy(dst, src, i + 1);
    tribe_uart_puts(dst);

    for (;;) {
    }
}
