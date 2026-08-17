#include "accelerator.h"
#include "uart.h"

static uint32_t source_data[16] = {
    0x10010001u, 0x10010002u, 0x10010003u, 0x10010004u,
    0x20020001u, 0x20020002u, 0x20020003u, 0x20020004u,
    0x30030001u, 0x30030002u, 0x30030003u, 0x30030004u,
    0x40040001u, 0x40040002u, 0x40040003u, 0x40040004u,
};

static uint32_t copy_back[16] __attribute__((aligned(4096))) = { 0x13579bdfu };
static uint32_t prbs_back[8] __attribute__((aligned(4096))) = { 0x2468ace0u };

int main(void)
{
    uint32_t seed = 0x12345678u;

    tribe_accel_dma_to_accel((uint32_t)source_data, 0, 16);
    tribe_accel_dma_to_memory(0, (uint32_t)copy_back, 16);
    tribe_accel_dma_to_accel((uint32_t)copy_back, 32, 16);
    tribe_accel_dma_to_memory(32, (uint32_t)copy_back, 16);

    tribe_accel_prbs(seed);
    tribe_accel_dma_to_memory(0, (uint32_t)prbs_back, 8);
    tribe_accel_dma_to_accel((uint32_t)prbs_back, 64, 8);
    tribe_accel_dma_to_memory(64, (uint32_t)prbs_back, 8);

    tribe_uart_puts("ACCELERATOR\n");
    return 0;
}
