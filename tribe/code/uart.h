#pragma once

#include <stddef.h>
#include <stdint.h>

#define TRIBE_UART_BASE 0x70000u

static inline void tribe_uart_putc(char ch)
{
    *(volatile uint32_t*)TRIBE_UART_BASE = (uint32_t)(uint8_t)ch;
}

static inline void tribe_uart_write(const char* data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        tribe_uart_putc(data[i]);
    }
}

static inline void tribe_uart_puts(const char* text)
{
    while (*text) {
        tribe_uart_putc(*text++);
    }
}

static inline void tribe_uart_put_i32(int32_t value)
{
    char buf[12];
    uint32_t n;
    int i = 0;

    if (value < 0) {
        tribe_uart_putc('-');
        n = (uint32_t)(-(value + 1)) + 1u;
    }
    else {
        n = (uint32_t)value;
    }

    do {
        buf[i++] = (char)('0' + (n % 10u));
        n /= 10u;
    } while (n != 0u);

    while (i > 0) {
        tribe_uart_putc(buf[--i]);
    }
}
