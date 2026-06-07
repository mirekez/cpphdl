#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#ifndef SYNTHESIS
#include <cstdio>
#include <cstdlib>
#include <print>
#endif

using namespace cpphdl;

#ifndef SYNTHESIS
extern long _system_clock;
#endif

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 64>
class EthGigDMA : public Module
{
public:
    static constexpr uint32_t XAXIDMA_TX_CR_OFFSET = 0x00;
    static constexpr uint32_t XAXIDMA_TX_SR_OFFSET = 0x04;
    static constexpr uint32_t XAXIDMA_TX_CDESC_OFFSET = 0x08;
    static constexpr uint32_t XAXIDMA_TX_TDESC_OFFSET = 0x10;
    static constexpr uint32_t XAXIDMA_RX_CR_OFFSET = 0x30;
    static constexpr uint32_t XAXIDMA_RX_SR_OFFSET = 0x34;
    static constexpr uint32_t XAXIDMA_RX_CDESC_OFFSET = 0x38;
    static constexpr uint32_t XAXIDMA_RX_TDESC_OFFSET = 0x40;

    static constexpr uint32_t XAXIDMA_CR_RUNSTOP_MASK = 0x00000001;
    static constexpr uint32_t XAXIDMA_CR_RESET_MASK = 0x00000004;
    static constexpr uint32_t XAXIDMA_SR_HALT_MASK = 0x00000001;
    static constexpr uint32_t XAXIDMA_IRQ_IOC_MASK = 0x00001000;
    static constexpr uint32_t XAXIDMA_IRQ_ERROR_MASK = 0x00004000;
    static constexpr uint32_t XAXIDMA_IRQ_ALL_MASK = 0x00007000;

    static constexpr uint32_t XAE_RAF_OFFSET = 0x000;
    static constexpr uint32_t XAE_IS_OFFSET = 0x00c;
    static constexpr uint32_t XAE_IP_OFFSET = 0x010;
    static constexpr uint32_t XAE_IE_OFFSET = 0x014;
    static constexpr uint32_t XAE_UAWL_OFFSET = 0x020;
    static constexpr uint32_t XAE_UAWU_OFFSET = 0x024;
    static constexpr uint32_t XAE_RCW0_OFFSET = 0x400;
    static constexpr uint32_t XAE_RCW1_OFFSET = 0x404;
    static constexpr uint32_t XAE_TC_OFFSET = 0x408;
    static constexpr uint32_t XAE_FCC_OFFSET = 0x40c;
    static constexpr uint32_t XAE_EMMC_OFFSET = 0x410;
    static constexpr uint32_t XAE_PHYC_OFFSET = 0x414;
    static constexpr uint32_t XAE_ID_OFFSET = 0x4f8;
    static constexpr uint32_t XAE_ABILITY_OFFSET = 0x4fc;
    static constexpr uint32_t XAE_MDIO_MC_OFFSET = 0x500;
    static constexpr uint32_t XAE_MDIO_MCR_OFFSET = 0x504;
    static constexpr uint32_t XAE_MDIO_MWD_OFFSET = 0x508;
    static constexpr uint32_t XAE_MDIO_MRD_OFFSET = 0x50c;
    static constexpr uint32_t XAE_UAW0_OFFSET = 0x700;
    static constexpr uint32_t XAE_UAW1_OFFSET = 0x704;
    static constexpr uint32_t XAE_FMI_OFFSET = 0x708;
    static constexpr uint32_t XAE_FFE_OFFSET = 0x70c;
    static constexpr uint32_t XAE_AF0_OFFSET = 0x710;
    static constexpr uint32_t XAE_AF1_OFFSET = 0x714;
    static constexpr uint32_t XAE_AM0_OFFSET = 0x750;
    static constexpr uint32_t XAE_AM1_OFFSET = 0x754;

    static constexpr uint32_t XAE_INT_PHYRSTCMPLT_MASK = 0x00000100;
    static constexpr uint32_t XAE_ABILITY_1G = 0x00000004;
    static constexpr uint32_t XAE_EMMC_RGMII_MASK = 0x20000000;
    static constexpr uint32_t XAE_EMMC_LINKSPD_1000 = 0x80000000;
    static constexpr uint32_t XAE_PHYC_RGLINKSPD_1000 = 0x00000008;
    static constexpr uint32_t XAE_PHYC_RGMIILINK_MASK = 0x00000001;
    static constexpr uint32_t XAE_MDIO_MCR_READY_MASK = 0x00000080;
    static constexpr uint32_t XAE_FMI_PM_MASK = 0x80000000;

    static constexpr uint32_t XAXIDMA_BD_NDESC_OFFSET = 0x00;
    static constexpr uint32_t XAXIDMA_BD_BUFA_OFFSET = 0x08;
    static constexpr uint32_t XAXIDMA_BD_CTRL_LEN_OFFSET = 0x18;
    static constexpr uint32_t XAXIDMA_BD_STS_OFFSET = 0x1c;
    static constexpr uint32_t XAXIDMA_BD_USR4_OFFSET = 0x30;
    static constexpr uint32_t XAXIDMA_BD_CTRL_LENGTH_MASK = 0x007fffff;
    static constexpr uint32_t XAXIDMA_BD_STS_ACTUAL_LEN_MASK = 0x007fffff;
    static constexpr uint32_t XAXIDMA_BD_STS_COMPLETE_MASK = 0x80000000;
    static constexpr uint32_t XAXIDMA_BD_STS_RXSOF_MASK = 0x08000000;
    static constexpr uint32_t XAXIDMA_BD_STS_RXEOF_MASK = 0x04000000;

    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> dma_out;

    _PORT(bool) mac_tx_valid_out = _ASSIGN_REG(mac_tx_valid_reg);
    _PORT(u<8>) mac_tx_data_out = _ASSIGN_REG(mac_tx_data_reg);
    _PORT(bool) mac_tx_last_out = _ASSIGN_REG(mac_tx_last_reg);
    _PORT(bool) mac_tx_ready_in;

    _PORT(bool) mac_rx_valid_in;
    _PORT(u<8>) mac_rx_data_in;
    _PORT(bool) mac_rx_last_in;
    _PORT(bool) mac_rx_ready_out = _ASSIGN(rx_run_comb_func() && state_reg == ST_IDLE && rx_desc_ready_reg && (uint32_t)rx_buffer_addr_reg != 0);

    _PORT(bool) tx_irq_out = _ASSIGN((bool)((tx_sr_reg & XAXIDMA_IRQ_IOC_MASK) && (tx_cr_reg & XAXIDMA_IRQ_IOC_MASK)));
    _PORT(bool) rx_irq_out = _ASSIGN((bool)((rx_sr_reg & XAXIDMA_IRQ_IOC_MASK) && (rx_cr_reg & XAXIDMA_IRQ_IOC_MASK)));
    _PORT(uint32_t) debug_state_out = _ASSIGN((uint32_t)state_reg);
    _PORT(uint32_t) debug_tx_sr_out = _ASSIGN((uint32_t)tx_sr_reg);
    _PORT(uint32_t) debug_rx_sr_out = _ASSIGN((uint32_t)rx_sr_reg);
    _PORT(logic<48>) local_mac_out = _ASSIGN(local_mac_comb_func());
    _PORT(bool) promisc_out = _ASSIGN((bool)((mac_fmi_reg & XAE_FMI_PM_MASK) != 0));

private:
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;

    static constexpr uint32_t ST_IDLE = 0;
    static constexpr uint32_t ST_TX_NDESC_AR = 1;
    static constexpr uint32_t ST_TX_NDESC_R = 2;
    static constexpr uint32_t ST_TX_CTRL_AR = 3;
    static constexpr uint32_t ST_TX_CTRL_R = 4;
    static constexpr uint32_t ST_TX_BUFA_AR = 5;
    static constexpr uint32_t ST_TX_BUFA_R = 6;
    static constexpr uint32_t ST_TX_DATA_AR = 7;
    static constexpr uint32_t ST_TX_DATA_R = 8;
    static constexpr uint32_t ST_TX_SEND = 9;
    static constexpr uint32_t ST_TX_STATUS_RMW_AR = 10;
    static constexpr uint32_t ST_TX_STATUS_RMW_R = 11;
    static constexpr uint32_t ST_TX_STATUS_AW = 12;
    static constexpr uint32_t ST_TX_STATUS_W = 13;
    static constexpr uint32_t ST_TX_STATUS_B = 14;
    static constexpr uint32_t ST_RX_NDESC_AR = 15;
    static constexpr uint32_t ST_RX_NDESC_R = 16;
    static constexpr uint32_t ST_RX_BUFA_AR = 17;
    static constexpr uint32_t ST_RX_BUFA_R = 18;
    static constexpr uint32_t ST_RX_RMW_AR = 19;
    static constexpr uint32_t ST_RX_RMW_R = 20;
    static constexpr uint32_t ST_RX_WRITE_AW = 21;
    static constexpr uint32_t ST_RX_WRITE_W = 22;
    static constexpr uint32_t ST_RX_WRITE_B = 23;
    static constexpr uint32_t ST_RX_STATUS_RMW_AR = 24;
    static constexpr uint32_t ST_RX_STATUS_RMW_R = 25;
    static constexpr uint32_t ST_RX_STATUS_AW = 26;
    static constexpr uint32_t ST_RX_STATUS_W = 27;
    static constexpr uint32_t ST_RX_STATUS_B = 28;

    reg<u32> tx_cr_reg;
    reg<u32> tx_sr_reg;
    reg<u32> tx_cdesc_reg;
    reg<u32> tx_tdesc_reg;
    reg<u32> rx_cr_reg;
    reg<u32> rx_sr_reg;
    reg<u32> rx_cdesc_reg;
    reg<u32> rx_tdesc_reg;

    reg<u<5>> state_reg;
    reg<u32> desc_addr_reg;
    reg<u32> rx_desc_addr_reg;
    reg<u32> tx_next_desc_reg;
    reg<u32> rx_next_desc_reg;
    reg<u32> buffer_addr_reg;
    reg<u32> rx_buffer_addr_reg;
    reg<u32> len_reg;
    reg<u32> offset_reg;
    reg<logic<DATA_WIDTH>> beat_reg;
    reg<u32> beat_valid_bytes_reg;
    reg<u32> byte_index_reg;
    reg<u1> rx_write_last_reg;
    reg<u1> rx_desc_ready_reg;

    reg<u1> read_valid_reg;
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u1> write_addr_valid_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<logic<DATA_WIDTH>> write_data_reg;
    reg<u1> write_resp_wait_reg;

    reg<u1> mac_tx_valid_reg;
    reg<u<8>> mac_tx_data_reg;
    reg<u1> mac_tx_last_reg;
    reg<u32> mac_raf_reg;
    reg<u32> mac_is_reg;
    reg<u32> mac_ie_reg;
    reg<u32> mac_uaw0_reg;
    reg<u32> mac_uaw1_reg;
    reg<u32> mac_rcw0_reg;
    reg<u32> mac_rcw1_reg;
    reg<u32> mac_tc_reg;
    reg<u32> mac_fcc_reg;
    reg<u32> mac_emmc_reg;
    reg<u32> mac_phyc_reg;
    reg<u32> mac_mdio_mc_reg;
    reg<u32> mac_mdio_mcr_reg;
    reg<u32> mac_mdio_mwd_reg;
    reg<u32> mac_mdio_mrd_reg;
    reg<u32> mac_fmi_reg;
    reg<u32> mac_ffe_reg;
    reg<u32> mac_af0_reg;
    reg<u32> mac_af1_reg;
    reg<u32> mac_am0_reg;
    reg<u32> mac_am1_reg;

    reg<u<ADDR_WIDTH>> axi_read_addr_reg;
    reg<u<ID_WIDTH>> axi_read_id_reg;
    reg<u1> axi_read_valid_reg;
    reg<logic<DATA_WIDTH>> axi_read_data_reg;
    reg<u<ADDR_WIDTH>> axi_write_addr_reg;
    reg<u<ID_WIDTH>> axi_write_id_reg;
    reg<u1> axi_write_addr_valid_reg;
    reg<u1> axi_write_resp_valid_reg;

    _LAZY_COMB(tx_run_comb, bool)
        return tx_run_comb = (tx_cr_reg & XAXIDMA_CR_RUNSTOP_MASK) != 0;
    }

    _LAZY_COMB(rx_run_comb, bool)
        return rx_run_comb = (rx_cr_reg & XAXIDMA_CR_RUNSTOP_MASK) != 0;
    }

    _LAZY_COMB(rx_next_desc_valid_comb, bool)
        return rx_next_desc_valid_comb = (uint32_t)rx_next_desc_reg != 0u && (((uint32_t)rx_next_desc_reg & 0x3fu) == 0u);
    }

    _LAZY_COMB(local_mac_comb, logic<48>)
        return local_mac_comb = (logic<48>)(((uint64_t)((uint32_t)mac_uaw1_reg & 0xffffu) << 32) | (uint32_t)mac_uaw0_reg);
    }

    uint32_t mac_read_word(uint32_t addr)
    {
        if (addr == XAE_RAF_OFFSET) return mac_raf_reg;
        if (addr == XAE_IS_OFFSET) return mac_is_reg | XAE_INT_PHYRSTCMPLT_MASK;
        if (addr == XAE_IP_OFFSET) return mac_is_reg & mac_ie_reg;
        if (addr == XAE_IE_OFFSET) return mac_ie_reg;
        if (addr == XAE_UAWL_OFFSET) return mac_uaw0_reg;
        if (addr == XAE_UAWU_OFFSET) return mac_uaw1_reg;
        if (addr == XAE_RCW0_OFFSET) return mac_rcw0_reg;
        if (addr == XAE_RCW1_OFFSET) return mac_rcw1_reg;
        if (addr == XAE_TC_OFFSET) return mac_tc_reg;
        if (addr == XAE_FCC_OFFSET) return mac_fcc_reg;
        if (addr == XAE_EMMC_OFFSET) return mac_emmc_reg;
        if (addr == XAE_PHYC_OFFSET) return mac_phyc_reg;
        if (addr == XAE_ID_OFFSET) return 0x08000000;
        if (addr == XAE_ABILITY_OFFSET) return XAE_ABILITY_1G;
        if (addr == XAE_MDIO_MC_OFFSET) return mac_mdio_mc_reg;
        if (addr == XAE_MDIO_MCR_OFFSET) return mac_mdio_mcr_reg | XAE_MDIO_MCR_READY_MASK;
        if (addr == XAE_MDIO_MWD_OFFSET) return mac_mdio_mwd_reg;
        if (addr == XAE_MDIO_MRD_OFFSET) return mac_mdio_mrd_reg;
        if (addr == XAE_UAW0_OFFSET) return mac_uaw0_reg;
        if (addr == XAE_UAW1_OFFSET) return mac_uaw1_reg;
        if (addr == XAE_FMI_OFFSET) return mac_fmi_reg;
        if (addr == XAE_FFE_OFFSET) return mac_ffe_reg;
        if (addr == XAE_AF0_OFFSET) return mac_af0_reg;
        if (addr == XAE_AF1_OFFSET) return mac_af1_reg;
        if (addr == XAE_AM0_OFFSET) return mac_am0_reg;
        if (addr == XAE_AM1_OFFSET) return mac_am1_reg;
        return 0;
    }

    _LAZY_COMB(mmio_word_comb, uint32_t)
        uint32_t addr;
        addr = (uint32_t)axi_read_addr_reg & 0xfffu;
        if (addr < 0x100u || addr >= 0x200u) {
            return mmio_word_comb = mac_read_word(addr);
        }
        if ((addr & 0xffu) == XAXIDMA_TX_CR_OFFSET) {
            return mmio_word_comb = tx_cr_reg;
        }
        if ((addr & 0xffu) == XAXIDMA_TX_SR_OFFSET) {
            return mmio_word_comb = tx_sr_reg | (tx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        if ((addr & 0xffu) == XAXIDMA_TX_CDESC_OFFSET) {
            return mmio_word_comb = tx_cdesc_reg;
        }
        if ((addr & 0xffu) == XAXIDMA_TX_TDESC_OFFSET) {
            return mmio_word_comb = tx_tdesc_reg;
        }
        if ((addr & 0xffu) == XAXIDMA_RX_CR_OFFSET) {
            return mmio_word_comb = rx_cr_reg;
        }
        if ((addr & 0xffu) == XAXIDMA_RX_SR_OFFSET) {
            return mmio_word_comb = rx_sr_reg | (rx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        if ((addr & 0xffu) == XAXIDMA_RX_CDESC_OFFSET) {
            return mmio_word_comb = rx_cdesc_reg;
        }
        if ((addr & 0xffu) == XAXIDMA_RX_TDESC_OFFSET) {
            return mmio_word_comb = rx_tdesc_reg;
        }
        return mmio_word_comb = 0;
    }

    _LAZY_COMB(mmio_read_data_comb, logic<DATA_WIDTH>)
        uint32_t lane;
        lane = ((uint32_t)axi_read_addr_reg % DATA_BYTES) / 4u;
        mmio_read_data_comb = 0;
        mmio_read_data_comb.bits(lane * 32 + 31, lane * 32) = mmio_word_comb_func();
        return mmio_read_data_comb;
    }

    logic<DATA_WIDTH> mmio_read_data_from_addr(uint32_t full_addr)
    {
        uint32_t addr;
        uint32_t lane;
        uint32_t word;
        logic<DATA_WIDTH> data;

        addr = full_addr & 0xfffu;
        word = 0;
        if (addr < 0x100u || addr >= 0x200u) {
            word = mac_read_word(addr);
        }
        else if ((addr & 0xffu) == XAXIDMA_TX_CR_OFFSET) {
            word = tx_cr_reg;
        }
        else if ((addr & 0xffu) == XAXIDMA_TX_SR_OFFSET) {
            word = tx_sr_reg | (tx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        else if ((addr & 0xffu) == XAXIDMA_TX_CDESC_OFFSET) {
            word = tx_cdesc_reg;
        }
        else if ((addr & 0xffu) == XAXIDMA_TX_TDESC_OFFSET) {
            word = tx_tdesc_reg;
        }
        else if ((addr & 0xffu) == XAXIDMA_RX_CR_OFFSET) {
            word = rx_cr_reg;
        }
        else if ((addr & 0xffu) == XAXIDMA_RX_SR_OFFSET) {
            word = rx_sr_reg | (rx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        else if ((addr & 0xffu) == XAXIDMA_RX_CDESC_OFFSET) {
            word = rx_cdesc_reg;
        }
        else if ((addr & 0xffu) == XAXIDMA_RX_TDESC_OFFSET) {
            word = rx_tdesc_reg;
        }

        lane = (full_addr % DATA_BYTES) / 4u;
        data = 0;
        data.bits(lane * 32 + 31, lane * 32) = word;
        return data;
    }

    _LAZY_COMB(mmio_write_word_comb, uint32_t)
        uint32_t lane;
        lane = ((uint32_t)axi_write_addr_reg % DATA_BYTES) / 4u;
        return mmio_write_word_comb = (uint32_t)axi_in.wdata_in().bits(lane * 32 + 31, lane * 32);
    }

    _LAZY_COMB(dma_read_word_comb, uint32_t)
        uint32_t lane;
        lane = ((uint32_t)read_addr_reg % DATA_BYTES) / 4u;
        return dma_read_word_comb = (uint32_t)dma_out.rdata_out().bits(lane * 32 + 31, lane * 32);
    }

    _LAZY_COMB(status_write_beat_comb, logic<DATA_WIDTH>)
        uint32_t lane;
        uint32_t word;
        uint32_t desc_addr;
        desc_addr = (state_reg == ST_RX_STATUS_RMW_AR || state_reg == ST_RX_STATUS_RMW_R ||
            state_reg == ST_RX_STATUS_AW || state_reg == ST_RX_STATUS_W || state_reg == ST_RX_STATUS_B) ?
            (uint32_t)rx_cdesc_reg : (uint32_t)tx_cdesc_reg;
        lane = ((desc_addr + XAXIDMA_BD_STS_OFFSET) % DATA_BYTES) / 4u;
        word = XAXIDMA_BD_STS_COMPLETE_MASK | ((uint32_t)len_reg & XAXIDMA_BD_STS_ACTUAL_LEN_MASK);
        if (state_reg == ST_RX_STATUS_RMW_AR || state_reg == ST_RX_STATUS_RMW_R ||
            state_reg == ST_RX_STATUS_AW || state_reg == ST_RX_STATUS_W || state_reg == ST_RX_STATUS_B) {
            word |= XAXIDMA_BD_STS_RXSOF_MASK | XAXIDMA_BD_STS_RXEOF_MASK;
        }
        status_write_beat_comb = 0;
        status_write_beat_comb.bits(lane * 32 + 31, lane * 32) = word;
        return status_write_beat_comb;
    }

public:
    void _assign()
    {
        axi_in.awready_out = _ASSIGN(!axi_write_addr_valid_reg && !axi_write_resp_valid_reg);
        axi_in.wready_out = _ASSIGN(axi_write_addr_valid_reg && !axi_write_resp_valid_reg);
        axi_in.bvalid_out = _ASSIGN_REG(axi_write_resp_valid_reg);
        axi_in.bid_out = _ASSIGN_REG(axi_write_id_reg);
        axi_in.arready_out = _ASSIGN(!axi_read_valid_reg);
        axi_in.rvalid_out = _ASSIGN_REG(axi_read_valid_reg);
        axi_in.rdata_out = _ASSIGN_REG(axi_read_data_reg);
        axi_in.rlast_out = _ASSIGN_REG(axi_read_valid_reg);
        axi_in.rid_out = _ASSIGN_REG(axi_read_id_reg);

        dma_out.arvalid_in = _ASSIGN_REG(read_valid_reg);
        dma_out.araddr_in = _ASSIGN_REG(read_addr_reg);
        dma_out.arid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.rready_in = _ASSIGN(read_valid_reg && dma_out.rvalid_out());
        dma_out.awvalid_in = _ASSIGN_REG(write_addr_valid_reg);
        dma_out.awaddr_in = _ASSIGN_REG(write_addr_reg);
        dma_out.awid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.wvalid_in = _ASSIGN(write_addr_valid_reg &&
            (state_reg == ST_TX_STATUS_W || state_reg == ST_RX_WRITE_W || state_reg == ST_RX_STATUS_W));
        dma_out.wdata_in = _ASSIGN_REG(write_data_reg);
        dma_out.wlast_in = _ASSIGN(write_addr_valid_reg &&
            (state_reg == ST_TX_STATUS_W || state_reg == ST_RX_WRITE_W || state_reg == ST_RX_STATUS_W));
        dma_out.bready_in = _ASSIGN_REG(write_resp_wait_reg);
    }

    void _work(bool reset)
    {
        uint32_t addr;
        uint32_t word;
        uint32_t limit;
        uint32_t lane;
        uint32_t start_lane;
        uint32_t consumed;
        uint32_t byte_value;
        logic<DATA_WIDTH> rx_beat;
        logic<DATA_WIDTH> merged_beat;
        uint32_t i;
        bool tx_ioc_clear;
        bool rx_ioc_clear;
#ifndef SYNTHESIS
        bool trace_eth;
        FILE* trace_eth_file;
        static FILE* trace_eth_file_cached = nullptr;
        static bool trace_eth_file_checked = false;
        trace_eth = std::getenv("TRIBE_TRACE_ETH_DMA") != nullptr;
        if (!trace_eth_file_checked) {
            trace_eth_file_checked = true;
            if (const char* path = std::getenv("TRIBE_TRACE_ETH_DMA_FILE")) {
                trace_eth_file_cached = std::fopen(path, "a");
                if (trace_eth_file_cached == nullptr) {
                    std::print("can't open TRIBE_TRACE_ETH_DMA_FILE '{}'\n", path);
                }
            }
        }
        trace_eth_file = trace_eth_file_cached != nullptr ? trace_eth_file_cached : stdout;
#endif

        mac_tx_valid_reg._next = false;
        mac_tx_last_reg._next = false;
        tx_ioc_clear = false;
        rx_ioc_clear = false;

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            axi_read_addr_reg._next = axi_in.araddr_in();
            axi_read_id_reg._next = axi_in.arid_in();
            axi_read_data_reg._next = mmio_read_data_from_addr((uint32_t)axi_in.araddr_in());
            axi_read_valid_reg._next = true;
        }
        else if (axi_read_valid_reg && axi_in.rready_in()) {
            axi_read_valid_reg._next = false;
        }

        if (axi_in.awvalid_in() && axi_in.awready_out()) {
            axi_write_addr_reg._next = axi_in.awaddr_in();
            axi_write_id_reg._next = axi_in.awid_in();
            axi_write_addr_valid_reg._next = true;
        }
        if (axi_write_addr_valid_reg && axi_in.wvalid_in() && axi_in.wready_out()) {
            addr = (uint32_t)axi_write_addr_reg & 0xfffu;
            word = mmio_write_word_comb_func();
            if (addr < 0x100u || addr >= 0x200u) {
                if (addr == XAE_RAF_OFFSET) mac_raf_reg._next = word;
                else if (addr == XAE_IS_OFFSET) mac_is_reg._next = mac_is_reg & ~word;
                else if (addr == XAE_IE_OFFSET) mac_ie_reg._next = word;
                else if (addr == XAE_UAWL_OFFSET || addr == XAE_UAW0_OFFSET) mac_uaw0_reg._next = word;
                else if (addr == XAE_UAWU_OFFSET || addr == XAE_UAW1_OFFSET) mac_uaw1_reg._next = word & 0xffffu;
                else if (addr == XAE_RCW0_OFFSET) mac_rcw0_reg._next = word;
                else if (addr == XAE_RCW1_OFFSET) mac_rcw1_reg._next = word;
                else if (addr == XAE_TC_OFFSET) mac_tc_reg._next = word;
                else if (addr == XAE_FCC_OFFSET) mac_fcc_reg._next = word;
                else if (addr == XAE_EMMC_OFFSET) mac_emmc_reg._next = word;
                else if (addr == XAE_PHYC_OFFSET) mac_phyc_reg._next = word;
                else if (addr == XAE_MDIO_MC_OFFSET) mac_mdio_mc_reg._next = word;
                else if (addr == XAE_MDIO_MCR_OFFSET) mac_mdio_mcr_reg._next = word | XAE_MDIO_MCR_READY_MASK;
                else if (addr == XAE_MDIO_MWD_OFFSET) mac_mdio_mwd_reg._next = word;
                else if (addr == XAE_MDIO_MRD_OFFSET) mac_mdio_mrd_reg._next = word;
                else if (addr == XAE_FMI_OFFSET) mac_fmi_reg._next = word;
                else if (addr == XAE_FFE_OFFSET) mac_ffe_reg._next = word;
                else if (addr == XAE_AF0_OFFSET) mac_af0_reg._next = word;
                else if (addr == XAE_AF1_OFFSET) mac_af1_reg._next = word;
                else if (addr == XAE_AM0_OFFSET) mac_am0_reg._next = word;
                else if (addr == XAE_AM1_OFFSET) mac_am1_reg._next = word;
            }
            else if ((addr & 0xffu) == XAXIDMA_TX_CR_OFFSET) {
                if (word & XAXIDMA_CR_RESET_MASK) {
                    tx_cr_reg._next = 0;
                    tx_sr_reg._next = XAXIDMA_SR_HALT_MASK;
                }
                else {
                    tx_cr_reg._next = word;
                    tx_sr_reg._next = tx_sr_reg & ~XAXIDMA_SR_HALT_MASK;
                }
            }
            else if ((addr & 0xffu) == XAXIDMA_TX_SR_OFFSET) {
                tx_sr_reg._next = tx_sr_reg & ~word;
                tx_ioc_clear = (word & XAXIDMA_IRQ_IOC_MASK) != 0;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-mmio cycle={} write TX_SR word={:08x} old={:08x} next={:08x}\n",
                        _system_clock, word, (uint32_t)tx_sr_reg, (uint32_t)((uint32_t)tx_sr_reg & ~word));
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if ((addr & 0xffu) == XAXIDMA_TX_CDESC_OFFSET) {
                tx_cdesc_reg._next = word;
            }
            else if ((addr & 0xffu) == XAXIDMA_TX_TDESC_OFFSET) {
                tx_tdesc_reg._next = word;
            }
            else if ((addr & 0xffu) == XAXIDMA_RX_CR_OFFSET) {
                if (word & XAXIDMA_CR_RESET_MASK) {
                    rx_cr_reg._next = 0;
                    rx_sr_reg._next = XAXIDMA_SR_HALT_MASK;
                    rx_desc_ready_reg._next = false;
                }
                else {
                    rx_cr_reg._next = word;
                    rx_sr_reg._next = rx_sr_reg & ~XAXIDMA_SR_HALT_MASK;
                }
            }
            else if ((addr & 0xffu) == XAXIDMA_RX_SR_OFFSET) {
                rx_sr_reg._next = rx_sr_reg & ~word;
                rx_ioc_clear = (word & XAXIDMA_IRQ_IOC_MASK) != 0;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-mmio cycle={} write RX_SR word={:08x} old={:08x} next={:08x}\n",
                        _system_clock, word, (uint32_t)rx_sr_reg, (uint32_t)((uint32_t)rx_sr_reg & ~word));
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if ((addr & 0xffu) == XAXIDMA_RX_CDESC_OFFSET) {
                rx_cdesc_reg._next = word;
                rx_desc_ready_reg._next = false;
                rx_buffer_addr_reg._next = 0;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-mmio cycle={} write RX_CDESC word={:08x}\n",
                        _system_clock, word);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if ((addr & 0xffu) == XAXIDMA_RX_TDESC_OFFSET) {
                rx_tdesc_reg._next = word;
                rx_desc_ready_reg._next = true;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-mmio cycle={} write RX_TDESC word={:08x} cdesc={:08x} buffer={:08x}\n",
                        _system_clock, word, (uint32_t)rx_cdesc_reg, (uint32_t)rx_buffer_addr_reg);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            axi_write_addr_valid_reg._next = false;
            axi_write_resp_valid_reg._next = true;
        }
        else if (axi_write_resp_valid_reg && axi_in.bready_in()) {
            axi_write_resp_valid_reg._next = false;
        }

        if (dma_out.rvalid_out() && read_valid_reg) {
            read_valid_reg._next = false;
            word = dma_read_word_comb_func();
            if (state_reg == ST_TX_NDESC_R) {
                tx_next_desc_reg._next = word;
                state_reg._next = ST_TX_CTRL_AR;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} TX_NDESC_R desc={:08x} addr={:08x} lane={} rdata={} next={:08x} tail={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)read_addr_reg,
                        ((uint32_t)read_addr_reg % DATA_BYTES) / 4u, dma_out.rdata_out(), word, (uint32_t)tx_tdesc_reg);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_RX_NDESC_R) {
                rx_next_desc_reg._next = word;
                state_reg._next = ST_RX_BUFA_AR;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} RX_NDESC_R desc={:08x} addr={:08x} lane={} rdata={} next={:08x} tail={:08x}\n",
                        _system_clock, (uint32_t)rx_desc_addr_reg, (uint32_t)read_addr_reg,
                        ((uint32_t)read_addr_reg % DATA_BYTES) / 4u, dma_out.rdata_out(), word, (uint32_t)rx_tdesc_reg);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_TX_CTRL_R) {
                len_reg._next = word & XAXIDMA_BD_CTRL_LENGTH_MASK;
                state_reg._next = ST_TX_BUFA_AR;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} TX_CTRL_R desc={:08x} addr={:08x} lane={} rdata={} ctrl={:08x} len={}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)read_addr_reg,
                        ((uint32_t)read_addr_reg % DATA_BYTES) / 4u, dma_out.rdata_out(), word,
                        word & XAXIDMA_BD_CTRL_LENGTH_MASK);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_TX_BUFA_R) {
                buffer_addr_reg._next = word;
                offset_reg._next = 0;
                state_reg._next = ST_TX_DATA_AR;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} TX_BUFA_R desc={:08x} addr={:08x} lane={} rdata={} buffer={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)read_addr_reg,
                        ((uint32_t)read_addr_reg % DATA_BYTES) / 4u, dma_out.rdata_out(), word);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_TX_DATA_R) {
                beat_reg._next = dma_out.rdata_out();
                limit = (uint32_t)len_reg - (uint32_t)offset_reg;
                start_lane = (uint32_t)read_addr_reg % DATA_BYTES;
                if (limit > DATA_BYTES - start_lane) {
                    limit = DATA_BYTES - start_lane;
                }
                beat_valid_bytes_reg._next = limit;
                byte_index_reg._next = start_lane;
                state_reg._next = ST_TX_SEND;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} TX_DATA_R desc={:08x} buffer={:08x} addr={:08x} offset={} start={} bytes={} rdata={}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)buffer_addr_reg,
                        (uint32_t)read_addr_reg, (uint32_t)offset_reg, start_lane, limit, dma_out.rdata_out());
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_RX_BUFA_R) {
                rx_buffer_addr_reg._next = word;
                offset_reg._next = 0;
                len_reg._next = 0;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                byte_index_reg._next = 0;
                state_reg._next = ST_IDLE;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} RX_BUFA_R desc={:08x} buffer={:08x} next={:08x} tail={:08x}\n",
                        _system_clock, (uint32_t)rx_desc_addr_reg, word, (uint32_t)rx_next_desc_reg, (uint32_t)rx_tdesc_reg);
                    std::fflush(trace_eth_file);
                }
#endif
            }
            else if (state_reg == ST_RX_RMW_R) {
                merged_beat = dma_out.rdata_out();
                start_lane = (uint32_t)byte_index_reg;
                for (i = 0; i < DATA_BYTES; ++i) {
                    if (i >= start_lane && i < start_lane + (uint32_t)beat_valid_bytes_reg) {
                        merged_beat.bits(i * 8 + 7, i * 8) = beat_reg.bits(i * 8 + 7, i * 8);
                    }
                }
                write_data_reg._next = merged_beat;
                write_addr_valid_reg._next = true;
                write_resp_wait_reg._next = true;
                state_reg._next = ST_RX_WRITE_AW;
            }
            else if (state_reg == ST_TX_STATUS_RMW_R || state_reg == ST_RX_STATUS_RMW_R) {
                merged_beat = dma_out.rdata_out();
                lane = (((state_reg == ST_RX_STATUS_RMW_R ? (uint32_t)rx_cdesc_reg : (uint32_t)tx_cdesc_reg) +
                    XAXIDMA_BD_STS_OFFSET) % DATA_BYTES) / 4u;
                merged_beat.bits(lane * 32 + 31, lane * 32) = status_write_beat_comb_func().bits(lane * 32 + 31, lane * 32);
                write_data_reg._next = merged_beat;
                write_addr_valid_reg._next = true;
                write_resp_wait_reg._next = true;
                if (state_reg == ST_RX_STATUS_RMW_R) {
                    state_reg._next = ST_RX_STATUS_AW;
                }
                else {
                    state_reg._next = ST_TX_STATUS_AW;
                }
            }
        }

        if (write_resp_wait_reg && dma_out.bvalid_out()) {
            write_resp_wait_reg._next = false;
            write_addr_valid_reg._next = false;
            if (state_reg == ST_TX_STATUS_B) {
                if (!tx_ioc_clear) {
                    tx_sr_reg._next = tx_sr_reg | XAXIDMA_IRQ_IOC_MASK;
                }
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} TX_STATUS_B desc={:08x} data={} old={:08x} clear={} next={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, write_data_reg, (uint32_t)tx_sr_reg, tx_ioc_clear,
                        tx_ioc_clear ? (uint32_t)tx_sr_reg : (uint32_t)((uint32_t)tx_sr_reg | XAXIDMA_IRQ_IOC_MASK));
                    std::fflush(trace_eth_file);
                }
#endif
                tx_cdesc_reg._next = tx_next_desc_reg;
                buffer_addr_reg._next = 0;
                len_reg._next = 0;
                offset_reg._next = 0;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                byte_index_reg._next = 0;
                state_reg._next = ST_IDLE;
            }
            else if (state_reg == ST_RX_WRITE_B) {
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} RX_WRITE_B addr={:08x} bytes={} data={}\n",
                        _system_clock, (uint32_t)write_addr_reg, (uint32_t)beat_valid_bytes_reg, write_data_reg);
                    std::fflush(trace_eth_file);
                }
#endif
                offset_reg._next = offset_reg + beat_valid_bytes_reg;
                len_reg._next = len_reg + beat_valid_bytes_reg;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                byte_index_reg._next = 0;
                if (rx_write_last_reg) {
                    read_addr_reg._next = ((uint32_t)rx_cdesc_reg + XAXIDMA_BD_STS_OFFSET) & ~(DATA_BYTES - 1u);
                    write_addr_reg._next = ((uint32_t)rx_cdesc_reg + XAXIDMA_BD_STS_OFFSET) & ~(DATA_BYTES - 1u);
                    read_valid_reg._next = true;
                    state_reg._next = ST_RX_STATUS_RMW_AR;
                }
                else {
                    state_reg._next = ST_IDLE;
                }
            }
            else if (state_reg == ST_RX_STATUS_B) {
                if (!rx_ioc_clear) {
                    rx_sr_reg._next = rx_sr_reg | XAXIDMA_IRQ_IOC_MASK;
                }
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} RX_STATUS_B desc={:08x} len={} data={} old={:08x} clear={} next={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)len_reg, write_data_reg, (uint32_t)rx_sr_reg, rx_ioc_clear,
                        rx_ioc_clear ? (uint32_t)rx_sr_reg : (uint32_t)((uint32_t)rx_sr_reg | XAXIDMA_IRQ_IOC_MASK));
                    std::fflush(trace_eth_file);
                }
#endif
                rx_desc_ready_reg._next = false;
                if (rx_next_desc_valid_comb_func()) {
                    rx_cdesc_reg._next = rx_next_desc_reg;
                }
                if (rx_next_desc_valid_comb_func() && (uint32_t)rx_cdesc_reg != (uint32_t)rx_tdesc_reg) {
                    rx_desc_ready_reg._next = true;
                }
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print(trace_eth_file, "ethdma-event cycle={} RX_ADVANCE old={:08x} next={:08x} valid={} tail={:08x} ready={}\n",
                        _system_clock, (uint32_t)rx_cdesc_reg, (uint32_t)rx_next_desc_reg, rx_next_desc_valid_comb_func(),
                        (uint32_t)rx_tdesc_reg,
                        rx_next_desc_valid_comb_func() && (uint32_t)rx_cdesc_reg != (uint32_t)rx_tdesc_reg);
                    std::fflush(trace_eth_file);
                }
#endif
                buffer_addr_reg._next = 0;
                rx_buffer_addr_reg._next = 0;
                len_reg._next = 0;
                offset_reg._next = 0;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                byte_index_reg._next = 0;
                state_reg._next = ST_IDLE;
            }
        }

        if (state_reg == ST_IDLE) {
            if (tx_run_comb_func() && (uint32_t)tx_cdesc_reg != 0 && (uint32_t)tx_tdesc_reg != 0 && (uint32_t)tx_cdesc_reg <= (uint32_t)tx_tdesc_reg) {
                desc_addr_reg._next = tx_cdesc_reg;
                read_addr_reg._next = tx_cdesc_reg + XAXIDMA_BD_NDESC_OFFSET;
                read_valid_reg._next = true;
                state_reg._next = ST_TX_NDESC_AR;
            }
            else if (rx_run_comb_func() && rx_desc_ready_reg && (uint32_t)rx_buffer_addr_reg == 0) {
                rx_desc_addr_reg._next = rx_cdesc_reg;
                read_addr_reg._next = rx_cdesc_reg + XAXIDMA_BD_NDESC_OFFSET;
                read_valid_reg._next = true;
                state_reg._next = ST_RX_NDESC_AR;
            }
        }
        else if (state_reg == ST_TX_NDESC_AR && dma_out.arready_out()) {
            state_reg._next = ST_TX_NDESC_R;
        }
        else if (state_reg == ST_TX_CTRL_AR) {
            read_addr_reg._next = desc_addr_reg + XAXIDMA_BD_CTRL_LEN_OFFSET;
            read_valid_reg._next = true;
            if (dma_out.arready_out()) {
                state_reg._next = ST_TX_CTRL_R;
            }
        }
        else if (state_reg == ST_TX_BUFA_AR) {
            read_addr_reg._next = desc_addr_reg + XAXIDMA_BD_BUFA_OFFSET;
            read_valid_reg._next = true;
            if (dma_out.arready_out()) {
                state_reg._next = ST_TX_BUFA_R;
            }
        }
        else if (state_reg == ST_TX_DATA_AR) {
            read_addr_reg._next = buffer_addr_reg + offset_reg;
            read_valid_reg._next = true;
            if (dma_out.arready_out()) {
                state_reg._next = ST_TX_DATA_R;
            }
        }
        else if (state_reg == ST_TX_SEND) {
            if (mac_tx_ready_in()) {
                lane = (uint32_t)byte_index_reg;
                start_lane = ((uint32_t)buffer_addr_reg + (uint32_t)offset_reg) % DATA_BYTES;
                consumed = lane - start_lane + 1u;
                byte_value = (uint32_t)beat_reg.bits(lane * 8 + 7, lane * 8);
                mac_tx_data_reg._next = (uint8_t)byte_value;
                mac_tx_valid_reg._next = true;
                mac_tx_last_reg._next = ((uint32_t)offset_reg + consumed) >= (uint32_t)len_reg;
                if (consumed >= (uint32_t)beat_valid_bytes_reg) {
                    offset_reg._next = offset_reg + beat_valid_bytes_reg;
                    if ((uint32_t)offset_reg + (uint32_t)beat_valid_bytes_reg >= (uint32_t)len_reg) {
                        read_addr_reg._next = ((uint32_t)desc_addr_reg + XAXIDMA_BD_STS_OFFSET) & ~(DATA_BYTES - 1u);
                        write_addr_reg._next = ((uint32_t)desc_addr_reg + XAXIDMA_BD_STS_OFFSET) & ~(DATA_BYTES - 1u);
                        read_valid_reg._next = true;
                        state_reg._next = ST_TX_STATUS_RMW_AR;
                    }
                    else {
                        state_reg._next = ST_TX_DATA_AR;
                    }
                }
                else {
                    byte_index_reg._next = byte_index_reg + 1;
                }
            }
        }
        else if (state_reg == ST_TX_STATUS_AW && dma_out.awready_out()) {
            state_reg._next = ST_TX_STATUS_W;
        }
        else if (state_reg == ST_TX_STATUS_W && dma_out.wready_out()) {
            state_reg._next = ST_TX_STATUS_B;
        }
        else if (state_reg == ST_TX_STATUS_RMW_AR && dma_out.arready_out()) {
            state_reg._next = ST_TX_STATUS_RMW_R;
        }
        else if (state_reg == ST_RX_NDESC_AR && dma_out.arready_out()) {
            state_reg._next = ST_RX_NDESC_R;
        }
        else if (state_reg == ST_RX_BUFA_AR) {
            read_addr_reg._next = rx_desc_addr_reg + XAXIDMA_BD_BUFA_OFFSET;
            read_valid_reg._next = true;
            if (dma_out.arready_out()) {
                state_reg._next = ST_RX_BUFA_R;
            }
        }
        else if (state_reg == ST_IDLE && mac_rx_valid_in() && rx_desc_ready_reg) {
            // handled by the idle branch above after the descriptor buffer address is loaded
        }

        if (state_reg == ST_IDLE && rx_run_comb_func() && rx_desc_ready_reg && mac_rx_valid_in() && (uint32_t)rx_buffer_addr_reg != 0) {
            start_lane = (uint32_t)byte_index_reg;
            if ((uint32_t)beat_valid_bytes_reg == 0) {
                start_lane = ((uint32_t)rx_buffer_addr_reg + (uint32_t)offset_reg) % DATA_BYTES;
                byte_index_reg._next = start_lane;
            }
            lane = start_lane + (uint32_t)beat_valid_bytes_reg;
            rx_beat = beat_reg;
            rx_beat.bits(lane * 8 + 7, lane * 8) = mac_rx_data_in();
            beat_reg._next = rx_beat;
            beat_valid_bytes_reg._next = beat_valid_bytes_reg + 1;
            if (lane + 1u >= DATA_BYTES || mac_rx_last_in()) {
                read_addr_reg._next = ((uint32_t)rx_buffer_addr_reg + (uint32_t)offset_reg) & ~(DATA_BYTES - 1u);
                write_addr_reg._next = ((uint32_t)rx_buffer_addr_reg + (uint32_t)offset_reg) & ~(DATA_BYTES - 1u);
                read_valid_reg._next = true;
                rx_write_last_reg._next = mac_rx_last_in();
                state_reg._next = ST_RX_RMW_AR;
            }
        }
        else if (state_reg == ST_RX_RMW_AR && dma_out.arready_out()) {
            state_reg._next = ST_RX_RMW_R;
        }
        else if (state_reg == ST_RX_WRITE_AW && dma_out.awready_out()) {
            state_reg._next = ST_RX_WRITE_W;
        }
        else if (state_reg == ST_RX_WRITE_W && dma_out.wready_out()) {
            state_reg._next = ST_RX_WRITE_B;
        }
        else if (state_reg == ST_RX_STATUS_AW) {
            desc_addr_reg._next = rx_cdesc_reg;
            write_addr_valid_reg._next = true;
            write_resp_wait_reg._next = true;
            if (dma_out.awready_out()) {
                state_reg._next = ST_RX_STATUS_W;
            }
        }
        else if (state_reg == ST_RX_STATUS_W && dma_out.wready_out()) {
            state_reg._next = ST_RX_STATUS_B;
        }
        else if (state_reg == ST_RX_STATUS_RMW_AR && dma_out.arready_out()) {
            state_reg._next = ST_RX_STATUS_RMW_R;
        }

        if (reset) {
            tx_cr_reg._next = 0;
            tx_sr_reg._next = XAXIDMA_SR_HALT_MASK;
            tx_cdesc_reg._next = 0;
            tx_tdesc_reg._next = 0;
            rx_cr_reg._next = 0;
            rx_sr_reg._next = XAXIDMA_SR_HALT_MASK;
            rx_cdesc_reg._next = 0;
            rx_tdesc_reg._next = 0;
            state_reg._next = ST_IDLE;
            desc_addr_reg._next = 0;
            rx_desc_addr_reg._next = 0;
            tx_next_desc_reg._next = 0;
            rx_next_desc_reg._next = 0;
            buffer_addr_reg._next = 0;
            rx_buffer_addr_reg._next = 0;
            len_reg._next = 0;
            offset_reg._next = 0;
            beat_reg._next = 0;
            beat_valid_bytes_reg._next = 0;
            byte_index_reg._next = 0;
            rx_write_last_reg._next = false;
            rx_desc_ready_reg._next = false;
            read_valid_reg._next = false;
            read_addr_reg._next = 0;
            write_addr_valid_reg._next = false;
            write_addr_reg._next = 0;
            write_data_reg._next = 0;
            write_resp_wait_reg._next = false;
            mac_tx_valid_reg._next = false;
            mac_tx_data_reg._next = 0;
            mac_tx_last_reg._next = false;
            mac_raf_reg._next = 0;
            mac_is_reg._next = XAE_INT_PHYRSTCMPLT_MASK;
            mac_ie_reg._next = 0;
            mac_uaw0_reg._next = 0x00000002u;
            mac_uaw1_reg._next = 0x00001100u;
            mac_rcw0_reg._next = 0;
            mac_rcw1_reg._next = 0;
            mac_tc_reg._next = 0;
            mac_fcc_reg._next = 0;
            mac_emmc_reg._next = XAE_EMMC_RGMII_MASK | XAE_EMMC_LINKSPD_1000;
            mac_phyc_reg._next = XAE_PHYC_RGMIILINK_MASK | XAE_PHYC_RGLINKSPD_1000;
            mac_mdio_mc_reg._next = 0;
            mac_mdio_mcr_reg._next = XAE_MDIO_MCR_READY_MASK;
            mac_mdio_mwd_reg._next = 0;
            mac_mdio_mrd_reg._next = 0xffffu;
            mac_fmi_reg._next = 0;
            mac_ffe_reg._next = 0;
            mac_af0_reg._next = 0;
            mac_af1_reg._next = 0;
            mac_am0_reg._next = 0;
            mac_am1_reg._next = 0;
            axi_read_valid_reg._next = false;
            axi_write_addr_valid_reg._next = false;
            axi_write_resp_valid_reg._next = false;
        }
    }

    void _strobe()
    {
        tx_cr_reg.strobe();
        tx_sr_reg.strobe();
        tx_cdesc_reg.strobe();
        tx_tdesc_reg.strobe();
        rx_cr_reg.strobe();
        rx_sr_reg.strobe();
        rx_cdesc_reg.strobe();
        rx_tdesc_reg.strobe();
        state_reg.strobe();
        desc_addr_reg.strobe();
        rx_desc_addr_reg.strobe();
        tx_next_desc_reg.strobe();
        rx_next_desc_reg.strobe();
        buffer_addr_reg.strobe();
        rx_buffer_addr_reg.strobe();
        len_reg.strobe();
        offset_reg.strobe();
        beat_reg.strobe();
        beat_valid_bytes_reg.strobe();
        byte_index_reg.strobe();
        rx_write_last_reg.strobe();
        rx_desc_ready_reg.strobe();
        read_valid_reg.strobe();
        read_addr_reg.strobe();
        write_addr_valid_reg.strobe();
        write_addr_reg.strobe();
        write_data_reg.strobe();
        write_resp_wait_reg.strobe();
        mac_tx_valid_reg.strobe();
        mac_tx_data_reg.strobe();
        mac_tx_last_reg.strobe();
        mac_raf_reg.strobe();
        mac_is_reg.strobe();
        mac_ie_reg.strobe();
        mac_uaw0_reg.strobe();
        mac_uaw1_reg.strobe();
        mac_rcw0_reg.strobe();
        mac_rcw1_reg.strobe();
        mac_tc_reg.strobe();
        mac_fcc_reg.strobe();
        mac_emmc_reg.strobe();
        mac_phyc_reg.strobe();
        mac_mdio_mc_reg.strobe();
        mac_mdio_mcr_reg.strobe();
        mac_mdio_mwd_reg.strobe();
        mac_mdio_mrd_reg.strobe();
        mac_fmi_reg.strobe();
        mac_ffe_reg.strobe();
        mac_af0_reg.strobe();
        mac_af1_reg.strobe();
        mac_am0_reg.strobe();
        mac_am1_reg.strobe();
        axi_read_addr_reg.strobe();
        axi_read_id_reg.strobe();
        axi_read_valid_reg.strobe();
        axi_read_data_reg.strobe();
        axi_write_addr_reg.strobe();
        axi_write_id_reg.strobe();
        axi_write_addr_valid_reg.strobe();
        axi_write_resp_valid_reg.strobe();
    }
};
