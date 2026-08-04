#pragma once

#include <stdint.h>

#define TRIBE_CLINT_BASE 0x70100u
#define TRIBE_CLINT_MSIP (TRIBE_CLINT_BASE + 0x0000u)
#define TRIBE_CLINT_MTIMECMP_LO (TRIBE_CLINT_BASE + 0x4000u)
#define TRIBE_CLINT_MTIMECMP_HI (TRIBE_CLINT_BASE + 0x4004u)
#define TRIBE_CLINT_MTIME_LO (TRIBE_CLINT_BASE + 0xBFF8u)
#define TRIBE_CLINT_MTIME_HI (TRIBE_CLINT_BASE + 0xBFFCu)

static inline void tribe_clint_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t*)addr = value;
}

static inline uint32_t tribe_clint_read32(uint32_t addr)
{
    return *(volatile uint32_t*)addr;
}

static inline uint64_t tribe_clint_mtime(void)
{
    uint32_t hi0;
    uint32_t lo;
    uint32_t hi1;
    do {
        hi0 = tribe_clint_read32(TRIBE_CLINT_MTIME_HI);
        lo = tribe_clint_read32(TRIBE_CLINT_MTIME_LO);
        hi1 = tribe_clint_read32(TRIBE_CLINT_MTIME_HI);
    } while (hi0 != hi1);
    return ((uint64_t)hi0 << 32) | lo;
}

static inline void tribe_clint_set_mtimecmp(uint64_t value)
{
    tribe_clint_write32(TRIBE_CLINT_MTIMECMP_HI, 0xffffffffu);
    tribe_clint_write32(TRIBE_CLINT_MTIMECMP_LO, (uint32_t)value);
    tribe_clint_write32(TRIBE_CLINT_MTIMECMP_HI, (uint32_t)(value >> 32));
}

static inline void tribe_clint_disable_timer(void)
{
    tribe_clint_write32(TRIBE_CLINT_MTIMECMP_LO, 0xffffffffu);
    tribe_clint_write32(TRIBE_CLINT_MTIMECMP_HI, 0xffffffffu);
}

static inline void tribe_csr_write_mtvec(void (*handler)(void))
{
    uintptr_t addr = (uintptr_t)handler;
    __asm__ volatile("csrw mtvec, %0" : : "r"(addr) : "memory");
}

static inline void tribe_csr_write_mscratch(uint32_t value)
{
    __asm__ volatile("csrw mscratch, %0" : : "r"(value) : "memory");
}

static inline uint32_t tribe_csr_read_mscratch(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, mscratch" : "=r"(value));
    return value;
}

static inline void tribe_csr_enable_machine_timer_interrupt(void)
{
    uint32_t mie = 1u << 7;
    __asm__ volatile("csrw mie, %0" : : "r"(mie) : "memory");
    __asm__ volatile("csrsi mstatus, 8" : : : "memory");
}
