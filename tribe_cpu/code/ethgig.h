#pragma once

#include <stdint.h>

#define TRIBE_ETHGIG_BASE 0x7E100u

#define ETH_DMA_TX_CR      0x00u
#define ETH_DMA_TX_SR      0x04u
#define ETH_DMA_TX_CDESC   0x08u
#define ETH_DMA_TX_TDESC   0x10u
#define ETH_DMA_RX_CR      0x30u
#define ETH_DMA_RX_SR      0x34u
#define ETH_DMA_RX_CDESC   0x38u
#define ETH_DMA_RX_TDESC   0x40u

#define ETH_DMA_CR_RUNSTOP 0x00000001u
#define ETH_DMA_CR_RESET   0x00000004u
#define ETH_DMA_IRQ_IOC    0x00001000u
#define ETH_DMA_IRQ_ERROR  0x00004000u
#define ETH_DMA_IRQ_ALL    0x00007000u

#define ETH_BD_NDESC       0x00u
#define ETH_BD_BUFA        0x08u
#define ETH_BD_CTRL_LEN    0x18u
#define ETH_BD_STS         0x1cu
#define ETH_BD_USR4        0x30u

#define ETH_BD_CTRL_TXSOF  0x08000000u
#define ETH_BD_CTRL_TXEOF  0x04000000u
#define ETH_BD_LEN_MASK    0x007fffffu
#define ETH_BD_STS_DONE    0x80000000u
#define ETH_BD_STS_RXSOF   0x08000000u
#define ETH_BD_STS_RXEOF   0x04000000u

static inline void eth_write32(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)(TRIBE_ETHGIG_BASE + reg) = value;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline uint32_t eth_read32(uint32_t reg)
{
    uint32_t value = *(volatile uint32_t*)(TRIBE_ETHGIG_BASE + reg);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}

static inline void eth_desc_write32(void* desc, uint32_t off, uint32_t value)
{
    *(volatile uint32_t*)((uintptr_t)desc + off) = value;
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline uint32_t eth_desc_read32(void* desc, uint32_t off)
{
    uint32_t value = *(volatile uint32_t*)((uintptr_t)desc + off);
    __asm__ volatile("fence rw, rw" ::: "memory");
    return value;
}

static inline void eth_dma_reset(void)
{
    eth_write32(ETH_DMA_TX_CR, ETH_DMA_CR_RESET);
    eth_write32(ETH_DMA_RX_CR, ETH_DMA_CR_RESET);
    eth_write32(ETH_DMA_TX_SR, ETH_DMA_IRQ_ALL);
    eth_write32(ETH_DMA_RX_SR, ETH_DMA_IRQ_ALL);
}

static inline void eth_dma_start_rx(void* desc)
{
    eth_write32(ETH_DMA_RX_CDESC, (uint32_t)(uintptr_t)desc);
    eth_write32(ETH_DMA_RX_CR, ETH_DMA_CR_RUNSTOP | ETH_DMA_IRQ_IOC | ETH_DMA_IRQ_ERROR);
    eth_write32(ETH_DMA_RX_TDESC, (uint32_t)(uintptr_t)desc);
}

static inline void eth_dma_start_tx(void* desc)
{
    eth_write32(ETH_DMA_TX_CDESC, (uint32_t)(uintptr_t)desc);
    eth_write32(ETH_DMA_TX_CR, ETH_DMA_CR_RUNSTOP | ETH_DMA_IRQ_IOC | ETH_DMA_IRQ_ERROR);
    eth_write32(ETH_DMA_TX_TDESC, (uint32_t)(uintptr_t)desc);
}
