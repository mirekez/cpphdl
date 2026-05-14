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

/*
 * The lazy retry handler below deliberately returns to the same faulting load.
 * Save the caller-visible registers that the C handler body may use so the
 * retried instruction sees the same operand registers it had at the fault. Use
 * a mapped static frame instead of sp because this bare-metal test does not map
 * a high kernel stack after enabling Sv32.
 */
#define SAVE_TRAP_VOLATILES() \
    __asm__ volatile( \
        "csrw sscratch, t6\n" \
        "la t6, lazy_trap_frame\n" \
        "sw ra, 0(t6)\n" \
        "sw t0, 4(t6)\n" \
        "sw t1, 8(t6)\n" \
        "sw t2, 12(t6)\n" \
        "sw a0, 16(t6)\n" \
        "sw a1, 20(t6)\n" \
        "sw a2, 24(t6)\n" \
        "sw a3, 28(t6)\n" \
        "sw a4, 32(t6)\n" \
        "sw a5, 36(t6)\n" \
        "sw a6, 40(t6)\n" \
        "sw a7, 44(t6)\n" \
        "sw t3, 48(t6)\n" \
        "sw t4, 52(t6)\n" \
        "sw t5, 56(t6)\n" \
        "csrr t5, sscratch\n" \
        "sw t5, 60(t6)\n" \
        : : : "memory")

#define RESTORE_TRAP_VOLATILES_AND_SRET() \
    __asm__ volatile( \
        "la t6, lazy_trap_frame\n" \
        "lw ra, 0(t6)\n" \
        "lw t0, 4(t6)\n" \
        "lw t1, 8(t6)\n" \
        "lw t2, 12(t6)\n" \
        "lw a0, 16(t6)\n" \
        "lw a1, 20(t6)\n" \
        "lw a2, 24(t6)\n" \
        "lw a3, 28(t6)\n" \
        "lw a4, 32(t6)\n" \
        "lw a5, 36(t6)\n" \
        "lw a6, 40(t6)\n" \
        "lw a7, 44(t6)\n" \
        "lw t3, 48(t6)\n" \
        "lw t4, 52(t6)\n" \
        "lw t5, 56(t6)\n" \
        "lw t6, 60(t6)\n" \
        "sret\n" \
        : : : "memory")

static uint32_t root_pt[1024] __attribute__((aligned(4096)));
static uint32_t leaf_pt[1024] __attribute__((aligned(4096)));
static uint32_t lazy_leaf_pt[1024] __attribute__((aligned(4096)));
static volatile uint32_t value_a = 0x11112222u;
static volatile uint32_t value_b = 0x33334444u;
static volatile uint32_t high_fetch_seen;
static volatile uint32_t high_target;
static volatile uint32_t trap_seen;
static volatile uint32_t lazy_trap_seen;
static volatile uint32_t lazy_trap_frame[16];

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

    /*
     * Scenario: enter Sv32 with a minimal address space. Code stays reachable
     * through an identity superpage, while data is reached through a separate
     * leaf page table so loads, stores, and AMOs exercise real translation.
     */
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

    /*
     * Scenario: a normal page fault handler changes sepc to skip a faulting
     * load. This checks scause/stval reporting before the retry-in-place case.
     */
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
    /*
     * Scenario: Linux commonly faults on a missing user PTE, installs the PTE,
     * executes sfence.vma, and returns to retry the original load. The CPU must
     * not advance sepc or preserve stale TLB/refill state across that retry.
     */
    write_stvec((uint32_t)&&lazy_load_fault_handler);
    root_pt[3] = 0;
    lazy_trap_seen = 0;
    alias_offset = ((uint32_t)&value_b) & 0xfffu;
    alias = (volatile uint32_t*)(0x00c00000u + alias_offset);
    if (*alias != 0x77778888u) {
        fail("lazy-load-value");
    }
    if (lazy_trap_seen != 1u) {
        fail("lazy-load-trap");
    }

    /*
     * Scenario: execute through a high Sv32 virtual alias after the lazy
     * fault path. Compute the target after the trap so the C handler does not
     * have to preserve a compiler temporary across the transparent retry.
     */
    high_fetch_seen = 0;
    high_target = ((uint32_t)&&high_fetch_label) | 0x40000000u;
    goto *((void*)high_target);

lazy_load_fault_handler:
{
    uint32_t expected_stval;
    SAVE_TRAP_VOLATILES();
    expected_stval = 0x00c00000u + (((uint32_t)&value_b) & 0xfffu);
    if (read_scause() != 13u) {
        fail("lazy-load-scause");
    }
    if (read_stval() != expected_stval) {
        fail("lazy-load-stval");
    }
    /*
     * The leaf slot is selected by the faulting virtual VPN0, while the PPN
     * points at the physical page containing value_b. Keeping those separate
     * catches alias mappings where virtual and physical page numbers differ.
     */
    lazy_leaf_pt[(expected_stval >> 12) & 0x3ffu] =
        pte(((uint32_t)&value_b) >> 12, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    root_pt[3] = pte(((uint32_t)lazy_leaf_pt) >> 12, PTE_V);
    sfence_vma();
    lazy_trap_seen++;
    RESTORE_TRAP_VOLATILES_AND_SRET();
}

high_fetch_label:
    high_fetch_seen = 0x13579bdfu;
    if (high_fetch_seen != 0x13579bdfu) {
        fail("high-fetch");
    }

    tribe_uart_puts("MMU_TLB\n");
    for (;;) {}
}
