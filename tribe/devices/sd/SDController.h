#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#include "sd/SDTypes.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 64, size_t FIFO_DEPTH = 32>
class SDController : public Module
{
public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> dma_out;

    _PORT(bool) sd_cmd_valid_out = _ASSIGN_REG(tx_valid_reg);
    _PORT(u<8>) sd_cmd_data_out = _ASSIGN_REG(tx_data_reg);
    _PORT(bool) sd_cmd_last_out = _ASSIGN_REG(tx_last_reg);
    _PORT(bool) sd_cmd_ready_in;

    _PORT(bool) sd_rsp_valid_in;
    _PORT(u<8>) sd_rsp_data_in;
    _PORT(bool) sd_rsp_last_in;
    _PORT(bool) sd_rsp_ready_out = _ASSIGN(!rx_valid_reg);

    _PORT(bool) irq_out = _ASSIGN((bool)(irq_pending_reg && irq_enable_reg));
    _PORT(bool) dma_write_complete_out = _ASSIGN_REG(dma_write_complete_reg);
    _PORT(uint32_t) debug_status_out = _ASSIGN_COMB(debug_status_comb_func());
    _PORT(uint32_t) debug_state_out = _ASSIGN((uint32_t)state_reg);
    _PORT(uint32_t) debug_count_out = _ASSIGN((uint32_t)count_reg);
    _PORT(uint32_t) debug_len_out = _ASSIGN((uint32_t)len_reg);

private:
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;
    static constexpr uint32_t HEADER_BYTES = 7;

    static constexpr uint32_t C_CMD17_READ_SINGLE_BLOCK = sd::CMD17_READ_SINGLE_BLOCK;
    static constexpr uint32_t C_CMD24_WRITE_SINGLE_BLOCK = sd::CMD24_WRITE_SINGLE_BLOCK;
    static constexpr uint32_t C_R1_READY = sd::R1_READY;
    static constexpr uint32_t C_DEFAULT_BLOCK_BYTES = sd::DEFAULT_BLOCK_BYTES;
    static constexpr uint32_t C_REG_CONTROL = sd::REG_CONTROL;
    static constexpr uint32_t C_REG_STATUS = sd::REG_STATUS;
    static constexpr uint32_t C_REG_CMD = sd::REG_CMD;
    static constexpr uint32_t C_REG_ARG = sd::REG_ARG;
    static constexpr uint32_t C_REG_LEN = sd::REG_LEN;
    static constexpr uint32_t C_REG_DMA_ADDR = sd::REG_DMA_ADDR;
    static constexpr uint32_t C_REG_TXDATA = 0x18;
    static constexpr uint32_t C_REG_RXDATA = sd::REG_RXDATA;
    static constexpr uint32_t C_REG_IRQ_ENABLE = sd::REG_IRQ_ENABLE;
    static constexpr uint32_t C_REG_IRQ_PENDING = sd::REG_IRQ_PENDING;
    static constexpr uint32_t C_REG_DMA_DESC_ADDR = 0x28;
    static constexpr uint32_t C_REG_DMA_DESC_LEN = 0x2c;
    static constexpr uint32_t C_REG_DMA_DESC_PUSH = 0x30;
    static constexpr uint32_t C_REG_DMA_DESC_STATUS = 0x34;
    static constexpr uint32_t C_CTRL_START = sd::CTRL_START;
    static constexpr uint32_t C_CTRL_WRITE = sd::CTRL_WRITE;
    static constexpr uint32_t C_CTRL_DMA = sd::CTRL_DMA;
    static constexpr uint32_t C_CTRL_CLEAR_DONE = sd::CTRL_CLEAR_DONE;
    static constexpr uint32_t C_STATUS_BUSY = sd::STATUS_BUSY;
    static constexpr uint32_t C_STATUS_DONE = sd::STATUS_DONE;
    static constexpr uint32_t C_STATUS_ERROR = sd::STATUS_ERROR;
    static constexpr uint32_t C_STATUS_RX_VALID = sd::STATUS_RX_VALID;
    static constexpr uint32_t C_STATUS_TX_READY = sd::STATUS_TX_READY;
    static constexpr uint32_t C_STATUS_IRQ = sd::STATUS_IRQ;
    static constexpr uint32_t C_STATUS_DESC_READY = 1u << 6;
    static constexpr uint32_t C_IRQ_DONE = sd::IRQ_DONE;
    static constexpr uint32_t C_DESC_STATUS_READY = 1u << 0;
    static constexpr uint32_t C_DESC_STATUS_EMPTY = 1u << 1;
    static constexpr uint32_t C_DESC_STATUS_FULL = 1u << 2;
    static constexpr uint32_t C_DESC_STATUS_COUNT_SHIFT = 8;

    static constexpr uint32_t ST_IDLE = 0;
    static constexpr uint32_t ST_HEADER = 1;
    static constexpr uint32_t ST_PIO_READ = 2;
    static constexpr uint32_t ST_PIO_WRITE = 3;
    static constexpr uint32_t ST_WAIT_ACK = 4;
    static constexpr uint32_t ST_DMA_READ_RECV = 5;
    static constexpr uint32_t ST_DMA_READ_AW = 6;
    static constexpr uint32_t ST_DMA_READ_W = 7;
    static constexpr uint32_t ST_DMA_READ_B = 8;
    static constexpr uint32_t ST_DMA_WRITE_AR = 9;
    static constexpr uint32_t ST_DMA_WRITE_R = 10;
    static constexpr uint32_t ST_DMA_WRITE_SEND = 11;
    static constexpr uint32_t ST_DONE = 12;
    static constexpr uint32_t ST_ERROR = 13;
    static constexpr uint32_t ST_DMA_LOAD_DESC = 14;

    static constexpr size_t FIFO_INDEX_BITS = clog2(FIFO_DEPTH);
    static constexpr size_t FIFO_COUNT_BITS = clog2(FIFO_DEPTH + 1);

    reg<array<u<8>, FIFO_DEPTH>> tx_fifo_data_reg;
    reg<u<FIFO_INDEX_BITS>> tx_fifo_rd_reg;
    reg<u<FIFO_INDEX_BITS>> tx_fifo_wr_reg;
    reg<u<FIFO_COUNT_BITS>> tx_fifo_count_reg;
    reg<array<u<8>, FIFO_DEPTH>> rx_fifo_data_reg;
    reg<u<FIFO_INDEX_BITS>> rx_fifo_rd_reg;
    reg<u<FIFO_INDEX_BITS>> rx_fifo_wr_reg;
    reg<u<FIFO_COUNT_BITS>> rx_fifo_count_reg;

    reg<u1> tx_valid_reg;
    reg<u<8>> tx_data_reg;
    reg<u1> tx_last_reg;
    reg<u1> rx_valid_reg;
    reg<u<8>> rx_data_reg;
    reg<u1> rx_last_reg;

    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<logic<DATA_WIDTH>> read_data_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;

    reg<u<8>> cmd_reg;
    reg<u32> arg_reg;
    reg<u32> len_reg;
    reg<u32> dma_addr_reg;
    reg<u32> dma_desc_addr_reg;
    reg<u32> dma_desc_len_reg;
    reg<u32> count_reg;
    reg<u<5>> state_reg;
    reg<u<8>> header_index_reg;
    reg<u1> write_mode_reg;
    reg<u1> dma_mode_reg;
    reg<u1> busy_reg;
    reg<u1> done_reg;
    reg<u1> error_reg;
    reg<u1> irq_enable_reg;
    reg<u1> irq_pending_reg;
    reg<u1> dma_write_complete_reg;

    reg<logic<DATA_WIDTH>> dma_beat_reg;
    reg<u32> dma_beat_base_reg;
    reg<u32> dma_byte_index_reg;
    reg<u32> dma_beat_limit_reg;
    reg<array<u32, FIFO_DEPTH>> desc_addr_data_reg;
    reg<array<u32, FIFO_DEPTH>> desc_len_data_reg;
    reg<u<FIFO_INDEX_BITS>> desc_rd_reg;
    reg<u<FIFO_INDEX_BITS>> desc_wr_reg;
    reg<u<FIFO_COUNT_BITS>> desc_count_reg;
    reg<u32> active_desc_addr_reg;
    reg<u32> active_desc_remaining_reg;
    reg<u1> active_desc_valid_reg;

    _LAZY_COMB(tx_fifo_full_comb, bool)
        return tx_fifo_full_comb = tx_fifo_count_reg == FIFO_DEPTH;
    }

    _LAZY_COMB(tx_fifo_valid_comb, bool)
        return tx_fifo_valid_comb = tx_fifo_count_reg != 0;
    }

    _LAZY_COMB(tx_fifo_data_comb, u<8>)
        return tx_fifo_data_comb = tx_fifo_data_reg[(uint32_t)tx_fifo_rd_reg];
    }

    _LAZY_COMB(tx_fifo_rd_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)tx_fifo_rd_reg + 1u >= FIFO_DEPTH) {
            return tx_fifo_rd_next_comb = 0;
        }
        return tx_fifo_rd_next_comb = tx_fifo_rd_reg + 1;
    }

    _LAZY_COMB(tx_fifo_wr_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)tx_fifo_wr_reg + 1u >= FIFO_DEPTH) {
            return tx_fifo_wr_next_comb = 0;
        }
        return tx_fifo_wr_next_comb = tx_fifo_wr_reg + 1;
    }

    _LAZY_COMB(rx_fifo_full_comb, bool)
        return rx_fifo_full_comb = rx_fifo_count_reg == FIFO_DEPTH;
    }

    _LAZY_COMB(rx_fifo_valid_comb, bool)
        return rx_fifo_valid_comb = rx_fifo_count_reg != 0;
    }

    _LAZY_COMB(rx_fifo_data_comb, u<8>)
        return rx_fifo_data_comb = rx_fifo_data_reg[(uint32_t)rx_fifo_rd_reg];
    }

    _LAZY_COMB(rx_fifo_rd_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)rx_fifo_rd_reg + 1u >= FIFO_DEPTH) {
            return rx_fifo_rd_next_comb = 0;
        }
        return rx_fifo_rd_next_comb = rx_fifo_rd_reg + 1;
    }

    _LAZY_COMB(rx_fifo_wr_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)rx_fifo_wr_reg + 1u >= FIFO_DEPTH) {
            return rx_fifo_wr_next_comb = 0;
        }
        return rx_fifo_wr_next_comb = rx_fifo_wr_reg + 1;
    }

    _LAZY_COMB(desc_fifo_full_comb, bool)
        return desc_fifo_full_comb = desc_count_reg == FIFO_DEPTH;
    }

    _LAZY_COMB(desc_fifo_valid_comb, bool)
        return desc_fifo_valid_comb = desc_count_reg != 0;
    }

    _LAZY_COMB(desc_fifo_rd_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)desc_rd_reg + 1u >= FIFO_DEPTH) {
            return desc_fifo_rd_next_comb = 0;
        }
        return desc_fifo_rd_next_comb = desc_rd_reg + 1;
    }

    _LAZY_COMB(desc_fifo_wr_next_comb, u<FIFO_INDEX_BITS>)
        if ((uint32_t)desc_wr_reg + 1u >= FIFO_DEPTH) {
            return desc_fifo_wr_next_comb = 0;
        }
        return desc_fifo_wr_next_comb = desc_wr_reg + 1;
    }

    _LAZY_COMB(dma_remaining_comb, uint32_t)
        uint32_t total_left;
        uint32_t desc_left;
        total_left = (uint32_t)len_reg - (uint32_t)count_reg;
        desc_left = active_desc_valid_reg ? (uint32_t)active_desc_remaining_reg : total_left;
        return dma_remaining_comb = desc_left < total_left ? desc_left : total_left;
    }

    _LAZY_COMB(dma_next_beat_limit_comb, uint32_t)
        uint32_t limit;
        limit = dma_remaining_comb_func();
        if (limit > DATA_BYTES) {
            limit = DATA_BYTES;
        }
        return dma_next_beat_limit_comb = limit;
    }

    _LAZY_COMB(write_word_comb, uint32_t)
        size_t lane;
        write_word_comb = 0;
        for (lane = 0; lane < DATA_BYTES / 4; ++lane) {
            write_word_comb |= (uint32_t)axi_in.wdata_in().bits(lane * 32 + 31, lane * 32);
        }
        return write_word_comb;
    }

    _LAZY_COMB(header_byte_comb, u<8>)
        uint32_t idx;
        idx = header_index_reg;
        if (idx == 0) {
            return header_byte_comb = cmd_reg;
        }
        if (idx >= 1 && idx <= 4) {
            return header_byte_comb = (uint8_t)(((uint32_t)arg_reg >> ((idx - 1u) * 8u)) & 0xffu);
        }
        if (idx == 5) {
            return header_byte_comb = (uint8_t)((uint32_t)len_reg & 0xffu);
        }
        return header_byte_comb = (uint8_t)(((uint32_t)len_reg >> 8) & 0xffu);
    }

    _LAZY_COMB(tx_byte_comb, u<8>)
        size_t i;
        uint32_t idx;
        tx_byte_comb = 0;
        idx = (uint32_t)dma_byte_index_reg % DATA_BYTES;
        for (i = 0; i < DATA_BYTES; ++i) {
            if (idx == i) {
                tx_byte_comb = (uint8_t)((uint32_t)dma_beat_reg.bits(i * 8 + 7, i * 8));
            }
        }
        return tx_byte_comb;
    }

    _LAZY_COMB(phy_tx_valid_comb, bool)
        if (state_reg == ST_HEADER) {
            return phy_tx_valid_comb = true;
        }
        if (state_reg == ST_PIO_WRITE) {
            return phy_tx_valid_comb = tx_fifo_valid_comb_func();
        }
        if (state_reg == ST_DMA_WRITE_SEND) {
            return phy_tx_valid_comb = count_reg < len_reg;
        }
        return phy_tx_valid_comb = false;
    }

    _LAZY_COMB(phy_tx_data_comb, u<8>)
        if (state_reg == ST_HEADER) {
            return phy_tx_data_comb = header_byte_comb_func();
        }
        if (state_reg == ST_PIO_WRITE) {
            return phy_tx_data_comb = tx_fifo_data_comb_func();
        }
        return phy_tx_data_comb = tx_byte_comb_func();
    }

    _LAZY_COMB(phy_tx_last_comb, bool)
        if (state_reg == ST_HEADER) {
            return phy_tx_last_comb = !write_mode_reg && header_index_reg == HEADER_BYTES - 1u;
        }
        if (state_reg == ST_PIO_WRITE || state_reg == ST_DMA_WRITE_SEND) {
            return phy_tx_last_comb = count_reg + 1u >= len_reg;
        }
        return phy_tx_last_comb = false;
    }

    _LAZY_COMB(phy_rx_ready_comb, bool)
        if (state_reg == ST_PIO_READ) {
            return phy_rx_ready_comb = !rx_fifo_full_comb_func();
        }
        if (state_reg == ST_DMA_READ_RECV || state_reg == ST_WAIT_ACK) {
            return phy_rx_ready_comb = true;
        }
        return phy_rx_ready_comb = false;
    }

    _LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        uint32_t raddr;
        uint32_t word;
        size_t lane;
        read_data_comb = 0;
        word = 0;
        raddr = axi_in.arvalid_in() ? (uint32_t)axi_in.araddr_in() : (uint32_t)read_addr_reg;
        if (raddr == 0x04u) {
            word = debug_status_comb_func();
        }
        else if (raddr == 0x08u) {
            word = cmd_reg;
        }
        else if (raddr == 0x0cu) {
            word = arg_reg;
        }
        else if (raddr == 0x10u) {
            word = len_reg;
        }
        else if (raddr == 0x14u) {
            word = dma_addr_reg;
        }
        else if (raddr == 0x1cu) {
            word = rx_fifo_valid_comb_func() ? (uint32_t)rx_fifo_data_comb_func() : 0u;
        }
        else if (raddr == 0x20u) {
            word = irq_enable_reg ? 0x01u : 0u;
        }
        else if (raddr == 0x24u) {
            word = irq_pending_reg ? 0x01u : 0u;
        }
        else if (raddr == C_REG_DMA_DESC_ADDR) {
            word = dma_desc_addr_reg;
        }
        else if (raddr == C_REG_DMA_DESC_LEN) {
            word = dma_desc_len_reg;
        }
        else if (raddr == C_REG_DMA_DESC_STATUS) {
            word = (!desc_fifo_full_comb_func() ? C_DESC_STATUS_READY : 0u) |
                   (!desc_fifo_valid_comb_func() ? C_DESC_STATUS_EMPTY : 0u) |
                   (desc_fifo_full_comb_func() ? C_DESC_STATUS_FULL : 0u) |
                   ((uint32_t)desc_count_reg << C_DESC_STATUS_COUNT_SHIFT);
        }
        // L2 can return an aligned AXI beat for an unaligned MMIO word load.
        // Broadcast 32-bit registers so the CPU lane extractor sees the value
        // regardless of the register offset inside a 64/128/256-bit beat.
        for (lane = 0; lane < DATA_BYTES / 4; ++lane) {
            read_data_comb.bits(lane * 32 + 31, lane * 32) = word;
        }
        return read_data_comb;
    }

    _LAZY_COMB(debug_status_comb, uint32_t)
        return debug_status_comb =
            (busy_reg ? 0x01u : 0u) |
            (done_reg ? 0x02u : 0u) |
            (error_reg ? 0x04u : 0u) |
            (rx_fifo_valid_comb_func() ? 0x08u : 0u) |
            (!tx_fifo_full_comb_func() ? 0x10u : 0u) |
            (irq_pending_reg ? 0x20u : 0u) |
            (!desc_fifo_full_comb_func() ? C_STATUS_DESC_READY : 0u);
    }

    _LAZY_COMB(dma_addr_comb, u<ADDR_WIDTH>)
        uint32_t addr;
        addr = active_desc_valid_reg ? (uint32_t)active_desc_addr_reg :
            (uint32_t)dma_addr_reg + (uint32_t)dma_beat_base_reg;
        return dma_addr_comb = (u<ADDR_WIDTH>)addr;
    }

public:
    void _assign()
    {
        axi_in.awready_out = _ASSIGN(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = _ASSIGN(write_addr_valid_reg && !write_resp_valid_reg &&
            ((uint32_t)write_addr_reg != C_REG_TXDATA || !tx_fifo_full_comb_func()) &&
            ((uint32_t)write_addr_reg != C_REG_DMA_DESC_PUSH || !desc_fifo_full_comb_func()));
        axi_in.bvalid_out = _ASSIGN_REG(write_resp_valid_reg);
        axi_in.bid_out = _ASSIGN_REG(write_id_reg);
        axi_in.arready_out = _ASSIGN(!read_valid_reg);
        axi_in.rvalid_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rdata_out = _ASSIGN_REG(read_data_reg);
        axi_in.rlast_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rid_out = _ASSIGN_REG(read_id_reg);

        dma_out.awvalid_in = _ASSIGN(state_reg == ST_DMA_READ_AW);
        dma_out.awaddr_in = _ASSIGN(dma_addr_comb_func());
        dma_out.awid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.wvalid_in = _ASSIGN(state_reg == ST_DMA_READ_W);
        dma_out.wdata_in = _ASSIGN_REG(dma_beat_reg);
        dma_out.wlast_in = _ASSIGN(state_reg == ST_DMA_READ_W);
        dma_out.bready_in = _ASSIGN(state_reg == ST_DMA_READ_B);
        dma_out.arvalid_in = _ASSIGN(state_reg == ST_DMA_WRITE_AR);
        dma_out.araddr_in = _ASSIGN(dma_addr_comb_func());
        dma_out.arid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.rready_in = _ASSIGN(state_reg == ST_DMA_WRITE_R);

    }

    void _work(bool reset)
    {
        uint32_t addr;
        uint32_t word;
        uint32_t next_count;
        uint32_t next_byte_index;
        uint32_t beat_limit;
        size_t i;
        logic<DATA_WIDTH> beat;
        bool tx_push;
        bool tx_pop;
        bool rx_push;
        bool rx_pop;
        bool desc_push;
        bool desc_pop;
        uint32_t next_desc_remaining;

        tx_push = false;
        desc_push = false;
        desc_pop = false;
        dma_write_complete_reg._next = false;
        if (tx_valid_reg && sd_cmd_ready_in()) {
            tx_valid_reg._next = false;
        }
        if (!tx_valid_reg && phy_tx_valid_comb_func()) {
            tx_valid_reg._next = true;
            tx_data_reg._next = phy_tx_data_comb_func();
            tx_last_reg._next = phy_tx_last_comb_func();
        }

        if (rx_valid_reg && phy_rx_ready_comb_func()) {
            rx_valid_reg._next = false;
        }
        if (!rx_valid_reg && sd_rsp_valid_in()) {
            rx_valid_reg._next = true;
            rx_data_reg._next = sd_rsp_data_in();
            rx_last_reg._next = sd_rsp_last_in();
        }

        tx_pop = state_reg == ST_PIO_WRITE && !tx_valid_reg && tx_fifo_valid_comb_func();
        rx_push = state_reg == ST_PIO_READ && rx_valid_reg && !rx_fifo_full_comb_func();
        rx_pop = axi_in.arvalid_in() && axi_in.arready_out() &&
            (uint32_t)axi_in.araddr_in() == 0x1cu && rx_fifo_valid_comb_func();

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            read_addr_reg._next = axi_in.araddr_in();
            read_id_reg._next = axi_in.arid_in();
            read_data_reg._next = read_data_comb_func();
            read_valid_reg._next = true;
        }
        if (read_valid_reg && axi_in.rready_in()) {
            read_valid_reg._next = false;
        }

        if (axi_in.awvalid_in() && axi_in.awready_out()) {
            write_addr_reg._next = axi_in.awaddr_in();
            write_id_reg._next = axi_in.awid_in();
            write_addr_valid_reg._next = true;
        }
        if (axi_in.wvalid_in() && axi_in.wready_out()) {
            addr = (uint32_t)write_addr_reg;
            word = write_word_comb_func();
            write_addr_valid_reg._next = false;
            write_resp_valid_reg._next = true;
            if (addr == 0x00u) {
                if ((word & 0x10u) && !(word & 0x01u)) {
                    done_reg._next = false;
                    error_reg._next = false;
                    irq_pending_reg._next = false;
                    desc_rd_reg.clr();
                    desc_wr_reg.clr();
                    desc_count_reg.clr();
                    active_desc_valid_reg.clr();
                    active_desc_addr_reg.clr();
                    active_desc_remaining_reg.clr();
                }
                if ((word & 0x01u) && !busy_reg) {
                    busy_reg._next = true;
                    done_reg._next = false;
                    error_reg._next = false;
                    irq_pending_reg._next = false;
                    write_mode_reg._next = (word & 0x02u) != 0;
                    dma_mode_reg._next = (word & 0x04u) != 0;
                    header_index_reg._next = 0;
                    count_reg._next = 0;
                    dma_beat_reg._next = 0;
                    dma_beat_base_reg._next = 0;
                    dma_byte_index_reg._next = 0;
                    dma_beat_limit_reg._next = 0;
                    active_desc_valid_reg._next = false;
                    active_desc_addr_reg._next = 0;
                    active_desc_remaining_reg._next = 0;
                    state_reg._next = ST_HEADER;
                    if ((word & 0x02u) != 0) {
                        cmd_reg._next = 0x58u;
                    }
                    else {
                        cmd_reg._next = 0x51u;
                    }
                }
            }
            else if (addr == 0x08u) {
                cmd_reg._next = (uint8_t)word;
            }
            else if (addr == 0x0cu) {
                arg_reg._next = word;
            }
            else if (addr == 0x10u) {
                len_reg._next = word;
            }
            else if (addr == 0x14u) {
                dma_addr_reg._next = word;
            }
            else if (addr == C_REG_DMA_DESC_ADDR) {
                dma_desc_addr_reg._next = word;
            }
            else if (addr == C_REG_DMA_DESC_LEN) {
                dma_desc_len_reg._next = word;
            }
            else if (addr == C_REG_DMA_DESC_PUSH && !desc_fifo_full_comb_func()) {
                desc_push = true;
                desc_addr_data_reg._next[(uint32_t)desc_wr_reg] = dma_desc_addr_reg;
                desc_len_data_reg._next[(uint32_t)desc_wr_reg] = dma_desc_len_reg;
                desc_wr_reg._next = desc_fifo_wr_next_comb_func();
            }
            else if (addr == 0x20u) {
                irq_enable_reg._next = (word & 0x01u) != 0;
            }
            else if (addr == 0x24u && (word & 0x01u)) {
                irq_pending_reg._next = false;
            }
            else if (addr == 0x18u && !tx_fifo_full_comb_func()) {
                tx_push = true;
                tx_fifo_data_reg._next[(uint32_t)tx_fifo_wr_reg] = (u<8>)(word & 0xffu);
                tx_fifo_wr_reg._next = tx_fifo_wr_next_comb_func();
            }
        }
        if (write_resp_valid_reg && axi_in.bready_in()) {
            write_resp_valid_reg._next = false;
        }

        if (tx_pop) {
            tx_fifo_rd_reg._next = tx_fifo_rd_next_comb_func();
        }
        if (tx_push && !tx_pop) {
            tx_fifo_count_reg._next = tx_fifo_count_reg + 1;
        }
        else if (!tx_push && tx_pop) {
            tx_fifo_count_reg._next = tx_fifo_count_reg - 1;
        }

        if (rx_push) {
            rx_fifo_data_reg._next[(uint32_t)rx_fifo_wr_reg] = sd_rsp_data_in();
            rx_fifo_wr_reg._next = rx_fifo_wr_next_comb_func();
        }
        if (rx_pop) {
            rx_fifo_rd_reg._next = rx_fifo_rd_next_comb_func();
        }
        if (rx_push && !rx_pop) {
            rx_fifo_count_reg._next = rx_fifo_count_reg + 1;
        }
        else if (!rx_push && rx_pop) {
            rx_fifo_count_reg._next = rx_fifo_count_reg - 1;
        }

        if (state_reg == ST_HEADER) {
            if (!tx_valid_reg) {
                if (header_index_reg + 1u >= HEADER_BYTES) {
                    count_reg._next = 0;
                    if (dma_mode_reg) {
                        state_reg._next = ST_DMA_LOAD_DESC;
                    }
                    else if (write_mode_reg) {
                        state_reg._next = ST_PIO_WRITE;
                    }
                    else {
                        state_reg._next = ST_PIO_READ;
                    }
                }
                else {
                    header_index_reg._next = header_index_reg + 1;
                }
            }
        }
        else if (state_reg == ST_PIO_READ) {
            if (rx_push) {
                if (count_reg + 1u >= len_reg) {
                    state_reg._next = ST_DONE;
                }
                count_reg._next = count_reg + 1;
            }
        }
        else if (state_reg == ST_PIO_WRITE) {
            if (tx_pop) {
                if (count_reg + 1u >= len_reg) {
                    state_reg._next = ST_WAIT_ACK;
                }
                count_reg._next = count_reg + 1;
            }
        }
        else if (state_reg == ST_WAIT_ACK) {
            if (rx_valid_reg) {
                if ((uint32_t)rx_data_reg == 0x00u) {
                    state_reg._next = ST_DONE;
                }
                else {
                    state_reg._next = ST_ERROR;
                }
            }
        }
        else if (state_reg == ST_DMA_LOAD_DESC) {
            dma_beat_reg._next = 0;
            dma_byte_index_reg._next = 0;
            dma_beat_limit_reg._next = 0;
            if (count_reg >= len_reg) {
                state_reg._next = write_mode_reg ? ST_WAIT_ACK : ST_DONE;
            }
            else if (desc_fifo_valid_comb_func()) {
                desc_pop = true;
                active_desc_valid_reg._next = true;
                active_desc_addr_reg._next = desc_addr_data_reg[(uint32_t)desc_rd_reg];
                active_desc_remaining_reg._next = desc_len_data_reg[(uint32_t)desc_rd_reg];
                desc_rd_reg._next = desc_fifo_rd_next_comb_func();
                state_reg._next = write_mode_reg ? ST_DMA_WRITE_AR : ST_DMA_READ_RECV;
            }
            else {
                // Backward-compatible single contiguous descriptor for older tests
                // and drivers that only program C_REG_DMA_ADDR plus C_REG_LEN.
                active_desc_valid_reg._next = true;
                active_desc_addr_reg._next = (uint32_t)dma_addr_reg + (uint32_t)count_reg;
                active_desc_remaining_reg._next = (uint32_t)len_reg - (uint32_t)count_reg;
                state_reg._next = write_mode_reg ? ST_DMA_WRITE_AR : ST_DMA_READ_RECV;
            }
        }
        else if (state_reg == ST_DMA_READ_RECV) {
            beat_limit = dma_beat_limit_reg ? (uint32_t)dma_beat_limit_reg : dma_next_beat_limit_comb_func();
            if (rx_valid_reg && beat_limit != 0) {
                if (dma_byte_index_reg == 0) {
                    dma_beat_limit_reg._next = beat_limit;
                }
                beat = dma_beat_reg;
                for (i = 0; i < DATA_BYTES; ++i) {
                    if ((uint32_t)dma_byte_index_reg == i) {
                        beat.bits(i * 8 + 7, i * 8) = rx_data_reg;
                    }
                }
                dma_beat_reg._next = beat;
                next_count = (uint32_t)count_reg + 1u;
                next_byte_index = (uint32_t)dma_byte_index_reg + 1u;
                next_desc_remaining = active_desc_remaining_reg ? (uint32_t)active_desc_remaining_reg - 1u : 0u;
                count_reg._next = next_count;
                dma_byte_index_reg._next = next_byte_index;
                active_desc_remaining_reg._next = next_desc_remaining;
                if (next_byte_index >= beat_limit || next_count >= (uint32_t)len_reg) {
                    dma_beat_limit_reg._next = next_byte_index;
                    state_reg._next = ST_DMA_READ_AW;
                }
            }
        }
        else if (state_reg == ST_DMA_READ_AW) {
            if (dma_out.awvalid_in() && dma_out.awready_out()) {
                state_reg._next = ST_DMA_READ_W;
            }
        }
        else if (state_reg == ST_DMA_READ_W) {
            if (dma_out.wvalid_in() && dma_out.wready_out()) {
                state_reg._next = ST_DMA_READ_B;
            }
        }
        else if (state_reg == ST_DMA_READ_B) {
            if (dma_out.bvalid_out() && dma_out.bready_in()) {
                dma_write_complete_reg._next = true;
                if (count_reg >= len_reg) {
                    state_reg._next = ST_DONE;
                }
                else if (active_desc_remaining_reg == 0) {
                    active_desc_valid_reg._next = false;
                    state_reg._next = ST_DMA_LOAD_DESC;
                }
                else {
                    active_desc_addr_reg._next = (uint32_t)active_desc_addr_reg + (uint32_t)dma_beat_limit_reg;
                    dma_beat_reg._next = 0;
                    dma_byte_index_reg._next = 0;
                    dma_beat_limit_reg._next = 0;
                    state_reg._next = ST_DMA_READ_RECV;
                }
            }
        }
        else if (state_reg == ST_DMA_WRITE_AR) {
            if (dma_out.arvalid_in() && dma_out.arready_out()) {
                state_reg._next = ST_DMA_WRITE_R;
            }
        }
        else if (state_reg == ST_DMA_WRITE_R) {
            if (dma_out.rvalid_out() && dma_out.rready_in()) {
                dma_beat_reg._next = dma_out.rdata_out();
                dma_byte_index_reg._next = 0;
                dma_beat_limit_reg._next = dma_next_beat_limit_comb_func();
                state_reg._next = ST_DMA_WRITE_SEND;
            }
        }
        else if (state_reg == ST_DMA_WRITE_SEND) {
            beat_limit = dma_beat_limit_reg;
            if (!tx_valid_reg && count_reg < len_reg && beat_limit != 0) {
                next_count = (uint32_t)count_reg + 1u;
                next_byte_index = (uint32_t)dma_byte_index_reg + 1u;
                next_desc_remaining = active_desc_remaining_reg ? (uint32_t)active_desc_remaining_reg - 1u : 0u;
                count_reg._next = next_count;
                dma_byte_index_reg._next = next_byte_index;
                active_desc_remaining_reg._next = next_desc_remaining;
                if (next_count >= (uint32_t)len_reg) {
                    state_reg._next = ST_WAIT_ACK;
                }
                else if (next_byte_index >= beat_limit) {
                    if (next_desc_remaining == 0) {
                        active_desc_valid_reg._next = false;
                        state_reg._next = ST_DMA_LOAD_DESC;
                    }
                    else {
                        active_desc_addr_reg._next = (uint32_t)active_desc_addr_reg + beat_limit;
                        dma_byte_index_reg._next = 0;
                        dma_beat_limit_reg._next = 0;
                        state_reg._next = ST_DMA_WRITE_AR;
                    }
                }
            }
        }
        else if (state_reg == ST_DONE) {
            busy_reg._next = false;
            done_reg._next = true;
            irq_pending_reg._next = true;
            state_reg._next = ST_IDLE;
        }
        else if (state_reg == ST_ERROR) {
            busy_reg._next = false;
            done_reg._next = true;
            error_reg._next = true;
            irq_pending_reg._next = true;
            state_reg._next = ST_IDLE;
        }

        if (desc_push && !desc_pop) {
            desc_count_reg._next = desc_count_reg + 1;
        }
        else if (!desc_push && desc_pop) {
            desc_count_reg._next = desc_count_reg - 1;
        }

        if (reset) {
            read_addr_reg.clr();
            read_id_reg.clr();
            read_valid_reg.clr();
            read_data_reg.clr();
            write_addr_reg.clr();
            write_id_reg.clr();
            write_addr_valid_reg.clr();
            write_resp_valid_reg.clr();
            cmd_reg._next = 0x51u;
            arg_reg.clr();
            len_reg._next = 512u;
            dma_addr_reg.clr();
            dma_desc_addr_reg.clr();
            dma_desc_len_reg.clr();
            count_reg.clr();
            state_reg.clr();
            header_index_reg.clr();
            write_mode_reg.clr();
            dma_mode_reg.clr();
            busy_reg.clr();
            done_reg.clr();
            error_reg.clr();
            irq_enable_reg.clr();
            irq_pending_reg.clr();
            dma_write_complete_reg.clr();
            dma_beat_reg.clr();
            dma_beat_base_reg.clr();
            dma_byte_index_reg.clr();
            dma_beat_limit_reg.clr();
            desc_rd_reg.clr();
            desc_wr_reg.clr();
            desc_count_reg.clr();
            active_desc_addr_reg.clr();
            active_desc_remaining_reg.clr();
            active_desc_valid_reg.clr();
            tx_fifo_rd_reg.clr();
            tx_fifo_wr_reg.clr();
            tx_fifo_count_reg.clr();
            rx_fifo_rd_reg.clr();
            rx_fifo_wr_reg.clr();
            rx_fifo_count_reg.clr();
            tx_valid_reg.clr();
            tx_data_reg.clr();
            tx_last_reg.clr();
            rx_valid_reg.clr();
            rx_data_reg.clr();
            rx_last_reg.clr();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        tx_fifo_data_reg.strobe(checkpoint_fd);
        tx_fifo_rd_reg.strobe(checkpoint_fd);
        tx_fifo_wr_reg.strobe(checkpoint_fd);
        tx_fifo_count_reg.strobe(checkpoint_fd);
        rx_fifo_data_reg.strobe(checkpoint_fd);
        rx_fifo_rd_reg.strobe(checkpoint_fd);
        rx_fifo_wr_reg.strobe(checkpoint_fd);
        rx_fifo_count_reg.strobe(checkpoint_fd);
        tx_valid_reg.strobe(checkpoint_fd);
        tx_data_reg.strobe(checkpoint_fd);
        tx_last_reg.strobe(checkpoint_fd);
        rx_valid_reg.strobe(checkpoint_fd);
        rx_data_reg.strobe(checkpoint_fd);
        rx_last_reg.strobe(checkpoint_fd);
        read_addr_reg.strobe(checkpoint_fd);
        read_id_reg.strobe(checkpoint_fd);
        read_valid_reg.strobe(checkpoint_fd);
        read_data_reg.strobe(checkpoint_fd);
        write_addr_reg.strobe(checkpoint_fd);
        write_id_reg.strobe(checkpoint_fd);
        write_addr_valid_reg.strobe(checkpoint_fd);
        write_resp_valid_reg.strobe(checkpoint_fd);
        cmd_reg.strobe(checkpoint_fd);
        arg_reg.strobe(checkpoint_fd);
        len_reg.strobe(checkpoint_fd);
        dma_addr_reg.strobe(checkpoint_fd);
        dma_desc_addr_reg.strobe(checkpoint_fd);
        dma_desc_len_reg.strobe(checkpoint_fd);
        count_reg.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        header_index_reg.strobe(checkpoint_fd);
        write_mode_reg.strobe(checkpoint_fd);
        dma_mode_reg.strobe(checkpoint_fd);
        busy_reg.strobe(checkpoint_fd);
        done_reg.strobe(checkpoint_fd);
        error_reg.strobe(checkpoint_fd);
        irq_enable_reg.strobe(checkpoint_fd);
        irq_pending_reg.strobe(checkpoint_fd);
        dma_write_complete_reg.strobe(checkpoint_fd);
        dma_beat_reg.strobe(checkpoint_fd);
        dma_beat_base_reg.strobe(checkpoint_fd);
        dma_byte_index_reg.strobe(checkpoint_fd);
        dma_beat_limit_reg.strobe(checkpoint_fd);
        desc_addr_data_reg.strobe(checkpoint_fd);
        desc_len_data_reg.strobe(checkpoint_fd);
        desc_rd_reg.strobe(checkpoint_fd);
        desc_wr_reg.strobe(checkpoint_fd);
        desc_count_reg.strobe(checkpoint_fd);
        active_desc_addr_reg.strobe(checkpoint_fd);
        active_desc_remaining_reg.strobe(checkpoint_fd);
        active_desc_valid_reg.strobe(checkpoint_fd);
    }
};
