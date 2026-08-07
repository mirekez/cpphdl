#pragma once

#include <stdint.h>

#include "SDTypes.h"

static inline void sd_write32(uintptr_t base, uint32_t off, uint32_t value)
{
    *(volatile uint32_t*)(base + off) = value;
}

static inline uint32_t sd_read32(uintptr_t base, uint32_t off)
{
    return *(volatile uint32_t*)(base + off);
}

static inline void sd_wait_done(uintptr_t base)
{
    while ((sd_read32(base, sd::REG_STATUS) & sd::STATUS_DONE) == 0) {
    }
}

static inline void sd_clear_done(uintptr_t base)
{
    sd_write32(base, sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
}

static inline void sd_pio_read(uintptr_t base, uint32_t block, uint8_t* dst, uint32_t len)
{
    sd_clear_done(base);
    sd_write32(base, sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
    sd_write32(base, sd::REG_ARG, block);
    sd_write32(base, sd::REG_LEN, len);
    sd_write32(base, sd::REG_CONTROL, sd::CTRL_START);
    for (uint32_t i = 0; i < len; ++i) {
        while ((sd_read32(base, sd::REG_STATUS) & sd::STATUS_RX_VALID) == 0) {
        }
        dst[i] = (uint8_t)sd_read32(base, sd::REG_RXDATA);
    }
    sd_wait_done(base);
}

static inline void sd_pio_write(uintptr_t base, uint32_t block, const uint8_t* src, uint32_t len)
{
    sd_clear_done(base);
    sd_write32(base, sd::REG_CMD, sd::CMD24_WRITE_SINGLE_BLOCK);
    sd_write32(base, sd::REG_ARG, block);
    sd_write32(base, sd::REG_LEN, len);
    for (uint32_t i = 0; i < len; ++i) {
        while ((sd_read32(base, sd::REG_STATUS) & sd::STATUS_TX_READY) == 0) {
        }
        sd_write32(base, sd::REG_TXDATA, src[i]);
    }
    sd_write32(base, sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_WRITE);
    sd_wait_done(base);
}

static inline void sd_dma_read(uintptr_t base, uint32_t block, uintptr_t dst, uint32_t len)
{
    sd_clear_done(base);
    sd_write32(base, sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
    sd_write32(base, sd::REG_ARG, block);
    sd_write32(base, sd::REG_LEN, len);
    sd_write32(base, sd::REG_DMA_ADDR, (uint32_t)dst);
    sd_write32(base, sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_DMA);
    sd_wait_done(base);
}

static inline void sd_dma_write(uintptr_t base, uint32_t block, uintptr_t src, uint32_t len)
{
    sd_clear_done(base);
    sd_write32(base, sd::REG_CMD, sd::CMD24_WRITE_SINGLE_BLOCK);
    sd_write32(base, sd::REG_ARG, block);
    sd_write32(base, sd::REG_LEN, len);
    sd_write32(base, sd::REG_DMA_ADDR, (uint32_t)src);
    sd_write32(base, sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_WRITE | sd::CTRL_DMA);
    sd_wait_done(base);
}

