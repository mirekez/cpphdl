#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#ifndef SYNTHESIS
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
    _PORT(bool) mac_rx_ready_out = _ASSIGN(rx_run_comb_func() && state_reg == ST_IDLE && rx_desc_ready_reg && (uint32_t)buffer_addr_reg != 0);

    _PORT(bool) tx_irq_out = _ASSIGN((bool)((tx_sr_reg & XAXIDMA_IRQ_IOC_MASK) && (tx_cr_reg & XAXIDMA_IRQ_IOC_MASK)));
    _PORT(bool) rx_irq_out = _ASSIGN((bool)((rx_sr_reg & XAXIDMA_IRQ_IOC_MASK) && (rx_cr_reg & XAXIDMA_IRQ_IOC_MASK)));
    _PORT(uint32_t) debug_state_out = _ASSIGN((uint32_t)state_reg);
    _PORT(uint32_t) debug_tx_sr_out = _ASSIGN((uint32_t)tx_sr_reg);
    _PORT(uint32_t) debug_rx_sr_out = _ASSIGN((uint32_t)rx_sr_reg);

private:
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;

    static constexpr uint32_t ST_IDLE = 0;
    static constexpr uint32_t ST_TX_CTRL_AR = 1;
    static constexpr uint32_t ST_TX_CTRL_R = 2;
    static constexpr uint32_t ST_TX_BUFA_AR = 3;
    static constexpr uint32_t ST_TX_BUFA_R = 4;
    static constexpr uint32_t ST_TX_DATA_AR = 5;
    static constexpr uint32_t ST_TX_DATA_R = 6;
    static constexpr uint32_t ST_TX_SEND = 7;
    static constexpr uint32_t ST_TX_STATUS_AW = 8;
    static constexpr uint32_t ST_TX_STATUS_W = 9;
    static constexpr uint32_t ST_TX_STATUS_B = 10;
    static constexpr uint32_t ST_RX_BUFA_AR = 11;
    static constexpr uint32_t ST_RX_BUFA_R = 12;
    static constexpr uint32_t ST_RX_WRITE_AW = 13;
    static constexpr uint32_t ST_RX_WRITE_W = 14;
    static constexpr uint32_t ST_RX_WRITE_B = 15;
    static constexpr uint32_t ST_RX_STATUS_AW = 16;
    static constexpr uint32_t ST_RX_STATUS_W = 17;
    static constexpr uint32_t ST_RX_STATUS_B = 18;

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
    reg<u32> buffer_addr_reg;
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

    _LAZY_COMB(mmio_word_comb, uint32_t)
        uint32_t addr;
        addr = (uint32_t)axi_read_addr_reg & 0xffu;
        if (addr == XAXIDMA_TX_CR_OFFSET) {
            return mmio_word_comb = tx_cr_reg;
        }
        if (addr == XAXIDMA_TX_SR_OFFSET) {
            return mmio_word_comb = tx_sr_reg | (tx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        if (addr == XAXIDMA_TX_CDESC_OFFSET) {
            return mmio_word_comb = tx_cdesc_reg;
        }
        if (addr == XAXIDMA_TX_TDESC_OFFSET) {
            return mmio_word_comb = tx_tdesc_reg;
        }
        if (addr == XAXIDMA_RX_CR_OFFSET) {
            return mmio_word_comb = rx_cr_reg;
        }
        if (addr == XAXIDMA_RX_SR_OFFSET) {
            return mmio_word_comb = rx_sr_reg | (rx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        if (addr == XAXIDMA_RX_CDESC_OFFSET) {
            return mmio_word_comb = rx_cdesc_reg;
        }
        if (addr == XAXIDMA_RX_TDESC_OFFSET) {
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

        addr = full_addr & 0xffu;
        word = 0;
        if (addr == XAXIDMA_TX_CR_OFFSET) {
            word = tx_cr_reg;
        }
        else if (addr == XAXIDMA_TX_SR_OFFSET) {
            word = tx_sr_reg | (tx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        else if (addr == XAXIDMA_TX_CDESC_OFFSET) {
            word = tx_cdesc_reg;
        }
        else if (addr == XAXIDMA_TX_TDESC_OFFSET) {
            word = tx_tdesc_reg;
        }
        else if (addr == XAXIDMA_RX_CR_OFFSET) {
            word = rx_cr_reg;
        }
        else if (addr == XAXIDMA_RX_SR_OFFSET) {
            word = rx_sr_reg | (rx_run_comb_func() ? 0u : XAXIDMA_SR_HALT_MASK);
        }
        else if (addr == XAXIDMA_RX_CDESC_OFFSET) {
            word = rx_cdesc_reg;
        }
        else if (addr == XAXIDMA_RX_TDESC_OFFSET) {
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
        desc_addr = (state_reg == ST_RX_STATUS_AW || state_reg == ST_RX_STATUS_W || state_reg == ST_RX_STATUS_B) ?
            (uint32_t)rx_cdesc_reg : (uint32_t)tx_cdesc_reg;
        lane = ((desc_addr + XAXIDMA_BD_STS_OFFSET) % DATA_BYTES) / 4u;
        word = XAXIDMA_BD_STS_COMPLETE_MASK | ((uint32_t)len_reg & XAXIDMA_BD_STS_ACTUAL_LEN_MASK);
        if (state_reg == ST_RX_STATUS_AW || state_reg == ST_RX_STATUS_W || state_reg == ST_RX_STATUS_B) {
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
        uint32_t byte_value;
        logic<DATA_WIDTH> rx_beat;
        bool tx_ioc_clear;
        bool rx_ioc_clear;
#ifndef SYNTHESIS
        bool trace_eth;
        trace_eth = std::getenv("TRIBE_TRACE_ETH_DMA") != nullptr;
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
            addr = (uint32_t)axi_write_addr_reg & 0xffu;
            word = mmio_write_word_comb_func();
            if (addr == XAXIDMA_TX_CR_OFFSET) {
                if (word & XAXIDMA_CR_RESET_MASK) {
                    tx_cr_reg._next = 0;
                    tx_sr_reg._next = XAXIDMA_SR_HALT_MASK;
                }
                else {
                    tx_cr_reg._next = word;
                    tx_sr_reg._next = tx_sr_reg & ~XAXIDMA_SR_HALT_MASK;
                }
            }
            else if (addr == XAXIDMA_TX_SR_OFFSET) {
                tx_sr_reg._next = tx_sr_reg & ~word;
                tx_ioc_clear = (word & XAXIDMA_IRQ_IOC_MASK) != 0;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print("ethdma-mmio cycle={} write TX_SR word={:08x} old={:08x} next={:08x}\n",
                        _system_clock, word, (uint32_t)tx_sr_reg, (uint32_t)((uint32_t)tx_sr_reg & ~word));
                }
#endif
            }
            else if (addr == XAXIDMA_TX_CDESC_OFFSET) {
                tx_cdesc_reg._next = word;
            }
            else if (addr == XAXIDMA_TX_TDESC_OFFSET) {
                tx_tdesc_reg._next = word;
            }
            else if (addr == XAXIDMA_RX_CR_OFFSET) {
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
            else if (addr == XAXIDMA_RX_SR_OFFSET) {
                rx_sr_reg._next = rx_sr_reg & ~word;
                rx_ioc_clear = (word & XAXIDMA_IRQ_IOC_MASK) != 0;
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print("ethdma-mmio cycle={} write RX_SR word={:08x} old={:08x} next={:08x}\n",
                        _system_clock, word, (uint32_t)rx_sr_reg, (uint32_t)((uint32_t)rx_sr_reg & ~word));
                }
#endif
            }
            else if (addr == XAXIDMA_RX_CDESC_OFFSET) {
                rx_cdesc_reg._next = word;
                rx_desc_ready_reg._next = false;
                buffer_addr_reg._next = 0;
            }
            else if (addr == XAXIDMA_RX_TDESC_OFFSET) {
                rx_tdesc_reg._next = word;
                rx_desc_ready_reg._next = true;
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
            if (state_reg == ST_TX_CTRL_R) {
                len_reg._next = word & XAXIDMA_BD_CTRL_LENGTH_MASK;
                state_reg._next = ST_TX_BUFA_AR;
            }
            else if (state_reg == ST_TX_BUFA_R) {
                buffer_addr_reg._next = word;
                offset_reg._next = 0;
                state_reg._next = ST_TX_DATA_AR;
            }
            else if (state_reg == ST_TX_DATA_R) {
                beat_reg._next = dma_out.rdata_out();
                limit = (uint32_t)len_reg - (uint32_t)offset_reg;
                if (limit > DATA_BYTES) {
                    limit = DATA_BYTES;
                }
                beat_valid_bytes_reg._next = limit;
                byte_index_reg._next = 0;
                state_reg._next = ST_TX_SEND;
            }
            else if (state_reg == ST_RX_BUFA_R) {
                buffer_addr_reg._next = word;
                offset_reg._next = 0;
                len_reg._next = 0;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                state_reg._next = ST_IDLE;
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
                    std::print("ethdma-event cycle={} TX_STATUS_B desc={:08x} data={} old={:08x} clear={} next={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, write_data_reg, (uint32_t)tx_sr_reg, tx_ioc_clear,
                        tx_ioc_clear ? (uint32_t)tx_sr_reg : (uint32_t)((uint32_t)tx_sr_reg | XAXIDMA_IRQ_IOC_MASK));
                }
#endif
                tx_cdesc_reg._next = 0;
                buffer_addr_reg._next = 0;
                offset_reg._next = 0;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                byte_index_reg._next = 0;
                state_reg._next = ST_IDLE;
            }
            else if (state_reg == ST_RX_WRITE_B) {
#ifndef SYNTHESIS
                if (trace_eth) {
                    std::print("ethdma-event cycle={} RX_WRITE_B addr={:08x} bytes={} data={}\n",
                        _system_clock, (uint32_t)write_addr_reg, (uint32_t)beat_valid_bytes_reg, write_data_reg);
                }
#endif
                offset_reg._next = offset_reg + beat_valid_bytes_reg;
                len_reg._next = len_reg + beat_valid_bytes_reg;
                beat_reg._next = 0;
                beat_valid_bytes_reg._next = 0;
                if (rx_write_last_reg) {
                    state_reg._next = ST_RX_STATUS_AW;
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
                    std::print("ethdma-event cycle={} RX_STATUS_B desc={:08x} len={} data={} old={:08x} clear={} next={:08x}\n",
                        _system_clock, (uint32_t)desc_addr_reg, (uint32_t)len_reg, write_data_reg, (uint32_t)rx_sr_reg, rx_ioc_clear,
                        rx_ioc_clear ? (uint32_t)rx_sr_reg : (uint32_t)((uint32_t)rx_sr_reg | XAXIDMA_IRQ_IOC_MASK));
                }
#endif
                rx_desc_ready_reg._next = false;
                state_reg._next = ST_IDLE;
            }
        }

        if (state_reg == ST_IDLE) {
            if (tx_run_comb_func() && (uint32_t)tx_cdesc_reg != 0 && (uint32_t)tx_tdesc_reg != 0 && (uint32_t)tx_cdesc_reg <= (uint32_t)tx_tdesc_reg) {
                desc_addr_reg._next = tx_cdesc_reg;
                read_addr_reg._next = tx_cdesc_reg + XAXIDMA_BD_CTRL_LEN_OFFSET;
                read_valid_reg._next = true;
                state_reg._next = ST_TX_CTRL_AR;
            }
            else if (rx_run_comb_func() && rx_desc_ready_reg && (uint32_t)buffer_addr_reg == 0) {
                desc_addr_reg._next = rx_cdesc_reg;
                read_addr_reg._next = rx_cdesc_reg + XAXIDMA_BD_BUFA_OFFSET;
                read_valid_reg._next = true;
                state_reg._next = ST_RX_BUFA_AR;
            }
        }
        else if (state_reg == ST_TX_CTRL_AR && dma_out.arready_out()) {
            state_reg._next = ST_TX_CTRL_R;
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
                byte_value = (uint32_t)beat_reg.bits(lane * 8 + 7, lane * 8);
                mac_tx_data_reg._next = (uint8_t)byte_value;
                mac_tx_valid_reg._next = true;
                mac_tx_last_reg._next = ((uint32_t)offset_reg + lane + 1u) >= (uint32_t)len_reg;
                if (lane + 1u >= (uint32_t)beat_valid_bytes_reg) {
                    offset_reg._next = offset_reg + beat_valid_bytes_reg;
                    if ((uint32_t)offset_reg + (uint32_t)beat_valid_bytes_reg >= (uint32_t)len_reg) {
                        write_addr_reg._next = desc_addr_reg + XAXIDMA_BD_STS_OFFSET;
                        write_data_reg._next = status_write_beat_comb_func();
                        write_addr_valid_reg._next = true;
                        write_resp_wait_reg._next = true;
                        state_reg._next = ST_TX_STATUS_AW;
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
        else if (state_reg == ST_RX_BUFA_AR && dma_out.arready_out()) {
            state_reg._next = ST_RX_BUFA_R;
        }
        else if (state_reg == ST_IDLE && mac_rx_valid_in() && rx_desc_ready_reg) {
            // handled by the idle branch above after the descriptor buffer address is loaded
        }

        if (state_reg == ST_IDLE && rx_run_comb_func() && rx_desc_ready_reg && mac_rx_valid_in() && (uint32_t)buffer_addr_reg != 0) {
            lane = (uint32_t)beat_valid_bytes_reg;
            rx_beat = beat_reg;
            rx_beat.bits(lane * 8 + 7, lane * 8) = mac_rx_data_in();
            beat_reg._next = rx_beat;
            beat_valid_bytes_reg._next = beat_valid_bytes_reg + 1;
            if (lane + 1u >= DATA_BYTES || mac_rx_last_in()) {
                write_addr_reg._next = buffer_addr_reg + offset_reg;
                write_data_reg._next = rx_beat;
                write_addr_valid_reg._next = true;
                write_resp_wait_reg._next = true;
                rx_write_last_reg._next = mac_rx_last_in();
                state_reg._next = ST_RX_WRITE_AW;
            }
        }
        else if (state_reg == ST_RX_WRITE_AW && dma_out.awready_out()) {
            state_reg._next = ST_RX_WRITE_W;
        }
        else if (state_reg == ST_RX_WRITE_W && dma_out.wready_out()) {
            state_reg._next = ST_RX_WRITE_B;
        }
        else if (state_reg == ST_RX_STATUS_AW) {
            desc_addr_reg._next = rx_cdesc_reg;
            write_addr_reg._next = rx_cdesc_reg + XAXIDMA_BD_STS_OFFSET;
            write_data_reg._next = status_write_beat_comb_func();
            write_addr_valid_reg._next = true;
            write_resp_wait_reg._next = true;
            if (dma_out.awready_out()) {
                state_reg._next = ST_RX_STATUS_W;
            }
        }
        else if (state_reg == ST_RX_STATUS_W && dma_out.wready_out()) {
            state_reg._next = ST_RX_STATUS_B;
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
            buffer_addr_reg._next = 0;
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
        buffer_addr_reg.strobe();
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
