#include <stdint.h>

#ifndef CPU_IRQ_INPUT_LEN
#define CPU_IRQ_INPUT_LEN 128u
#endif

#define UART_BASE 0x70000u
#define UART_RBR  (UART_BASE + 0u)
#define UART_THR  (UART_BASE + 0u)
#define UART_IER  (UART_BASE + 1u)
#define UART_IIR  (UART_BASE + 2u)
#define UART_LCR  (UART_BASE + 3u)
#define UART_MCR  (UART_BASE + 4u)
#define UART_LSR  (UART_BASE + 5u)

#define UART_IER_RX_AVAILABLE 0x01u
#define UART_IIR_RX_AVAILABLE 0x04u
#define UART_LSR_DR           0x01u
#define UART_LSR_THRE         0x20u
#define UART_MCR_OUT2         0x08u

#define PLIC_BASE             0x80000u
#define PLIC_PRIORITY(source) (PLIC_BASE + ((source) * 4u))
#define PLIC_ENABLE           (PLIC_BASE + 0x002000u)
#define PLIC_THRESHOLD        (PLIC_BASE + 0x200000u)
#define PLIC_CLAIM            (PLIC_BASE + 0x200004u)
#define PLIC_UART_SOURCE      1u

static volatile uint32_t rx_count;
static volatile uint32_t done_written;
static volatile uint32_t atomic_word = 0x10203040u;

extern void checkpoint_trap_entry(void);

static inline void write8(uint32_t addr, uint8_t value)
{
    *(volatile uint8_t*)addr = value;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline uint8_t read8(uint32_t addr)
{
    uint8_t value = *(volatile uint8_t*)addr;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}

static inline void write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t*)addr = value;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline uint32_t read32(uint32_t addr)
{
    uint32_t value = *(volatile uint32_t*)addr;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}

static void uart_putc(uint8_t ch)
{
    while ((read8(UART_LSR) & UART_LSR_THRE) == 0u) {
    }
    write32(UART_THR, ch);
}

static void uart_puts(const char* text)
{
    while (*text) {
        uart_putc((uint8_t)*text++);
    }
}

static void finish_if_done(void)
{
    if (!done_written && rx_count >= CPU_IRQ_INPUT_LEN) {
        done_written = 1;
        uart_puts("IRQATOMIC\n");
        uart_puts("DONE\n");
    }
}

void checkpoint_trap_handler(void)
{
    uint32_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause == 0x8000000bu) {
        uint32_t claim = read32(PLIC_CLAIM);
        while (claim == PLIC_UART_SOURCE) {
            uint32_t iir = read8(UART_IIR);
            if ((iir & 0x0fu) == UART_IIR_RX_AVAILABLE || (read8(UART_LSR) & UART_LSR_DR) != 0u) {
                uint8_t ch = (uint8_t)read32(UART_RBR);
                if (rx_count < CPU_IRQ_INPUT_LEN) {
                    ++rx_count;
                    uart_putc(ch);
                }
            }
            write32(PLIC_CLAIM, claim);
            claim = read32(PLIC_CLAIM);
        }
        finish_if_done();
        return;
    }

    uart_puts("FAIL\n");
    done_written = 1;
}

int main(void)
{
    rx_count = 0;
    done_written = 0;

    __asm__ volatile("csrw mtvec, %0" : : "r"(checkpoint_trap_entry) : "memory");

    write8(UART_LCR, 0x03u);
    write8(UART_MCR, UART_MCR_OUT2);
    write8(UART_IER, UART_IER_RX_AVAILABLE);

    write32(PLIC_PRIORITY(PLIC_UART_SOURCE), 1u);
    write32(PLIC_ENABLE, 1u << PLIC_UART_SOURCE);
    write32(PLIC_THRESHOLD, 0u);

    __asm__ volatile("csrs mie, %0" : : "r"(1u << 11) : "memory");
    __asm__ volatile("csrsi mstatus, 8" : : : "memory");

    uart_puts("READY\n");

    for (;;) {
        uint32_t tmp;
        uint32_t status;
        __asm__ volatile(
            "0:\n"
            "lr.w %[tmp], (%[addr])\n"
            "addi %[tmp], %[tmp], 1\n"
            "sc.w %[status], %[tmp], (%[addr])\n"
            "bnez %[status], 0b\n"
            : [tmp] "=&r"(tmp), [status] "=&r"(status)
            : [addr] "r"(&atomic_word)
            : "memory");
        finish_if_done();
    }
}
