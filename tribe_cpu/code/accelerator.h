#pragma once

#include <stdint.h>

#define TRIBE_ACCELERATOR_BASE 0x7C100u

#define TRIBE_ACCEL_SRC_ADDR   0x00u
#define TRIBE_ACCEL_DST_ADDR   0x04u
#define TRIBE_ACCEL_LEN_WORDS  0x08u
#define TRIBE_ACCEL_CONTROL    0x0cu
#define TRIBE_ACCEL_STATUS     0x10u
#define TRIBE_ACCEL_PRBS_SEED  0x14u
#define TRIBE_ACCEL_MEM_BASE   0x1000u

#define TRIBE_ACCEL_CTRL_START 0x01u
#define TRIBE_ACCEL_CTRL_A2M   0x02u
#define TRIBE_ACCEL_CTRL_PRBS  0x04u

static inline void tribe_accel_write32(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)(TRIBE_ACCELERATOR_BASE + reg) = value;
}

static inline uint32_t tribe_accel_read32(uint32_t reg)
{
    return *(volatile uint32_t*)(TRIBE_ACCELERATOR_BASE + reg);
}

static inline void tribe_accel_wait(void)
{
    while ((tribe_accel_read32(TRIBE_ACCEL_STATUS) & 0x2u) == 0u) {
        (void)tribe_accel_read32(TRIBE_ACCEL_CONTROL);
    }
}

static inline void tribe_accel_dma_to_accel(uint32_t main_src, uint32_t accel_dst_word, uint32_t words)
{
    tribe_accel_write32(TRIBE_ACCEL_SRC_ADDR, main_src);
    tribe_accel_write32(TRIBE_ACCEL_DST_ADDR, accel_dst_word);
    tribe_accel_write32(TRIBE_ACCEL_LEN_WORDS, words);
    tribe_accel_write32(TRIBE_ACCEL_CONTROL, TRIBE_ACCEL_CTRL_START);
    tribe_accel_wait();
}

static inline void tribe_accel_dma_to_memory(uint32_t accel_src_word, uint32_t main_dst, uint32_t words)
{
    tribe_accel_write32(TRIBE_ACCEL_SRC_ADDR, accel_src_word);
    tribe_accel_write32(TRIBE_ACCEL_DST_ADDR, main_dst);
    tribe_accel_write32(TRIBE_ACCEL_LEN_WORDS, words);
    tribe_accel_write32(TRIBE_ACCEL_CONTROL, TRIBE_ACCEL_CTRL_START | TRIBE_ACCEL_CTRL_A2M);
    tribe_accel_wait();
}

static inline void tribe_accel_prbs(uint32_t seed)
{
    tribe_accel_write32(TRIBE_ACCEL_PRBS_SEED, seed);
    tribe_accel_write32(TRIBE_ACCEL_CONTROL, TRIBE_ACCEL_CTRL_START | TRIBE_ACCEL_CTRL_PRBS);
    tribe_accel_wait();
}
