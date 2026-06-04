#include "ethgig.h"
#include "uart.h"

#include <stdint.h>

#define PLIC_BASE             0x80000u
#define PLIC_PRIORITY(source) (PLIC_BASE + ((source) * 4u))
#define PLIC_ENABLE           (PLIC_BASE + 0x002000u)
#define PLIC_THRESHOLD        (PLIC_BASE + 0x200000u)
#define PLIC_CLAIM            (PLIC_BASE + 0x200004u)
#define PLIC_ETH_SOURCE       3u

extern "C" void checkpoint_trap_entry(void);

static volatile uint32_t tx_irq_seen;
static volatile uint32_t rx_irq_seen;
static volatile uint32_t fail_seen;

static uint8_t tx_packet[18] __attribute__((aligned(64)));
static uint8_t rx_packet[64] __attribute__((aligned(64)));
static uint32_t tx_desc[16] __attribute__((aligned(64)));
static uint32_t rx_desc[16] __attribute__((aligned(64)));

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

extern "C" void checkpoint_trap_handler(void)
{
    uint32_t mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause == 0x8000000bu) {
        uint32_t claim = read32(PLIC_CLAIM);
        if (claim == PLIC_ETH_SOURCE) {
            uint32_t tx_sr = eth_read32(ETH_DMA_TX_SR);
            uint32_t rx_sr = eth_read32(ETH_DMA_RX_SR);
            if ((tx_sr & ETH_DMA_IRQ_IOC) != 0u) {
                tx_irq_seen = 1;
                eth_write32(ETH_DMA_TX_SR, ETH_DMA_IRQ_IOC);
            }
            if ((rx_sr & ETH_DMA_IRQ_IOC) != 0u) {
                rx_irq_seen = 1;
                eth_write32(ETH_DMA_RX_SR, ETH_DMA_IRQ_IOC);
            }
            if ((tx_sr & ETH_DMA_IRQ_ERROR) != 0u || (rx_sr & ETH_DMA_IRQ_ERROR) != 0u) {
                fail_seen = 1;
                eth_write32(ETH_DMA_TX_SR, ETH_DMA_IRQ_ERROR);
                eth_write32(ETH_DMA_RX_SR, ETH_DMA_IRQ_ERROR);
            }
            write32(PLIC_CLAIM, claim);
        }
        else if (claim != 0u) {
            write32(PLIC_CLAIM, claim);
        }
        return;
    }

    fail_seen = 1;
}

static void prepare_descriptors(void)
{
    static const uint8_t packet_template[18] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x11,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x22,
        0x08, 0x00, 0x45, 0x54, 0x48, 0x31,
    };

    for (unsigned i = 0; i < 16; ++i) {
        tx_desc[i] = 0;
        rx_desc[i] = 0;
    }
    for (unsigned i = 0; i < sizeof(tx_packet); ++i) {
        tx_packet[i] = packet_template[i];
    }
    for (unsigned i = 0; i < sizeof(rx_packet); ++i) {
        rx_packet[i] = 0xa5u;
    }

    eth_desc_write32(tx_desc, ETH_BD_BUFA, (uint32_t)(uintptr_t)tx_packet);
    eth_desc_write32(tx_desc, ETH_BD_CTRL_LEN,
        ETH_BD_CTRL_TXSOF | ETH_BD_CTRL_TXEOF | (uint32_t)sizeof(tx_packet));

    eth_desc_write32(rx_desc, ETH_BD_BUFA, (uint32_t)(uintptr_t)rx_packet);
    eth_desc_write32(rx_desc, ETH_BD_CTRL_LEN, (uint32_t)sizeof(rx_packet));
}

static int packet_matches(void)
{
    for (unsigned i = 0; i < sizeof(tx_packet); ++i) {
        if (rx_packet[i] != tx_packet[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    tx_irq_seen = 0;
    rx_irq_seen = 0;
    fail_seen = 0;

    __asm__ volatile("csrw mtvec, %0" : : "r"(checkpoint_trap_entry) : "memory");

    prepare_descriptors();
    eth_dma_reset();
    write32(PLIC_PRIORITY(PLIC_ETH_SOURCE), 1u);
    write32(PLIC_ENABLE, 1u << PLIC_ETH_SOURCE);
    write32(PLIC_THRESHOLD, 0u);
    __asm__ volatile("csrs mie, %0" : : "r"(1u << 11) : "memory");
    __asm__ volatile("csrsi mstatus, 8" : : : "memory");

    eth_dma_start_rx(rx_desc);
    eth_dma_start_tx(tx_desc);

    for (uint32_t guard = 0; guard < 200000u && (!tx_irq_seen || !rx_irq_seen) && !fail_seen; ++guard) {
        __asm__ volatile("nop" ::: "memory");
    }

    if (!tx_irq_seen || !rx_irq_seen || fail_seen) {
        tribe_uart_puts("FI\n");
        return 1;
    }

    for (uint32_t guard = 0; guard < 4096u; ++guard) {
        __asm__ volatile("nop" ::: "memory");
    }

    if ((eth_desc_read32(tx_desc, ETH_BD_STS) & ETH_BD_STS_DONE) == 0u) {
        tribe_uart_puts("FT\n");
        return 1;
    }
    uint32_t rx_sts = eth_desc_read32(rx_desc, ETH_BD_STS);
    if ((rx_sts & ETH_BD_STS_DONE) == 0u) {
        tribe_uart_puts("R0\n");
        return 1;
    }
    if ((rx_sts & ETH_BD_LEN_MASK) != sizeof(tx_packet)) {
        tribe_uart_puts("R1\n");
        return 1;
    }
    if (!packet_matches()) {
        tribe_uart_puts("FP\n");
        return 1;
    }

    tribe_uart_puts("ETHGIG\n");
    tribe_uart_puts("DONE\n");
    return 0;
}
