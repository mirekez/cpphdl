#include "uart.h"
#include <stdint.h>

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_X (1u << 3)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

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

static inline void write_stvec(uint32_t value)
{
    __asm__ volatile("csrw stvec, %0" : : "r"(value) : "memory");
}

static inline uint32_t read_scause(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, scause" : "=r"(value));
    return value;
}

static inline uint32_t read_stval(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, stval" : "=r"(value));
    return value;
}

static inline void write_sepc(uint32_t value)
{
    __asm__ volatile("csrw sepc, %0" : : "r"(value) : "memory");
}

static inline void sfence_vma(void)
{
    __asm__ volatile(".word 0x12000073" : : : "memory");
}

static inline uint32_t amoswap_w(volatile uint32_t* addr, uint32_t value)
{
    uint32_t old;
    __asm__ volatile("amoswap.w %0, %2, (%1)" : "=r"(old) : "r"(addr), "r"(value) : "memory");
    return old;
}

static uint32_t root_pt[1024] __attribute__((aligned(4096)));
static uint32_t leaf_pt[1024] __attribute__((aligned(4096)));
static volatile uint32_t value_a = 0x11112222u;
static volatile uint32_t value_b = 0x33334444u;
static volatile uint32_t high_fetch_seen;
static volatile uint32_t trap_seen;

static void fail(const char* reason)
{
    tribe_uart_puts("FAIL MMU_TLB ");
    tribe_uart_puts(reason);
    tribe_uart_puts("\n");
    for (;;) {}
}

static uint32_t pte(uint32_t ppn, uint32_t flags)
{
    return (ppn << 10) | flags;
}

void _start(void)
{
    volatile uint32_t* alias;
    uint32_t alias_base;
    uint32_t alias_offset;
    uint32_t flags;

    write_satp(0);
    sfence_vma();

    if (read_satp() != 0) {
        fail("satp-clear");
    }

    flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
    root_pt[0] = pte(0, flags); // Identity-map the first 4 MiB as a superpage for code.
    root_pt[1] = pte(((uint32_t)leaf_pt) >> 12, PTE_V);
    root_pt[0x100] = pte(0, flags); // Execute the same code through a high virtual alias.
    alias_base = 0x00400000u;
    alias_offset = ((uint32_t)&value_a) & 0xfffu;
    alias = (volatile uint32_t*)(alias_base + alias_offset);
    leaf_pt[0] = pte(((uint32_t)&value_a) >> 12, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);

    write_satp(0x80000000u | (((uint32_t)root_pt) >> 12));
    sfence_vma();

    if (*alias != 0x11112222u) {
        fail("alias-load-a");
    }

    *alias = 0x55556666u;
    if (value_a != 0x55556666u) {
        fail("alias-store-a");
    }

    leaf_pt[0] = pte(((uint32_t)&value_b) >> 12, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    sfence_vma();
    alias_offset = ((uint32_t)&value_b) & 0xfffu;
    alias = (volatile uint32_t*)(alias_base + alias_offset);
    if (*alias != 0x33334444u) {
        fail("alias-load-b");
    }
    if (amoswap_w(alias, 0x77778888u) != 0x33334444u || value_b != 0x77778888u) {
        fail("alias-amo-b");
    }

    write_stvec((uint32_t)&&load_fault_handler);
    trap_seen = 0;
faulting_load:
    __asm__ volatile("lw zero, 0(%0)" : : "r"(0x00c00000u) : "memory");
after_load_fault:
    if (!trap_seen) {
        fail("load-fault-missed");
    }
    goto after_load_fault_handler;

load_fault_handler:
    if (read_scause() != 13u) {
        fail("load-fault-scause");
    }
    if (read_stval() != 0x00c00000u) {
        fail("load-fault-stval");
    }
    trap_seen = 1;
    write_sepc((uint32_t)&&after_load_fault);
    __asm__ volatile("sret" : : : "memory");

after_load_fault_handler:
    high_fetch_seen = 0;
    goto *((void*)(((uint32_t)&&high_fetch_label) | 0x40000000u));

high_fetch_label:
    high_fetch_seen = 0x13579bdfu;
    if (high_fetch_seen != 0x13579bdfu) {
        fail("high-fetch");
    }

    tribe_uart_puts("MMU_TLB\n");
    for (;;) {}
}
