#pragma once

#include "cpphdl.h"
#include "Axi4.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256, size_t MEM_WORDS = 256>
class Accelerator : public Module
{
public:
    static constexpr uint32_t REG_SRC_ADDR = 0x00;
    static constexpr uint32_t REG_DST_ADDR = 0x04;
    static constexpr uint32_t REG_LEN_WORDS = 0x08;
    static constexpr uint32_t REG_CONTROL = 0x0c;
    static constexpr uint32_t REG_STATUS = 0x10;
    static constexpr uint32_t REG_PRBS_SEED = 0x14;
    static constexpr uint32_t REG_MEM_BASE = 0x1000;

    static constexpr uint32_t CTRL_START = 1u << 0;
    static constexpr uint32_t CTRL_DIR_A2M = 1u << 1;
    static constexpr uint32_t CTRL_PRBS = 1u << 2;

    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> dma_out;

private:
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;
    static constexpr uint32_t ST_IDLE = 0;
    static constexpr uint32_t ST_DMA_AR = 1;
    static constexpr uint32_t ST_DMA_R = 2;
    static constexpr uint32_t ST_DMA_AW = 3;
    static constexpr uint32_t ST_DMA_W = 4;
    static constexpr uint32_t ST_DMA_B = 5;
    static constexpr uint32_t ST_DONE = 6;

    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<logic<DATA_WIDTH>> read_data_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;

    reg<u32> src_addr_reg;
    reg<u32> dst_addr_reg;
    reg<u32> len_words_reg;
    reg<u32> index_reg;
    reg<u32> prbs_reg;
    reg<u1> busy_reg;
    reg<u1> done_reg;
    reg<u1> error_reg;
    reg<u1> dma_write_reg;
    reg<u<3>> state_reg;
    reg<array<u32, MEM_WORDS>> accel_mem_reg;

    _LAZY_COMB(read_word_comb, uint32_t)
        uint32_t addr;
        uint32_t mem_index;
        addr = (uint32_t)read_addr_reg;
        if (addr >= 0x100u && addr < 0x120u) {
            addr -= 0x100u;
        }
        read_word_comb = 0;
        if (addr == REG_SRC_ADDR) {
            read_word_comb = src_addr_reg;
        }
        else if (addr == REG_DST_ADDR) {
            read_word_comb = dst_addr_reg;
        }
        else if (addr == REG_LEN_WORDS) {
            read_word_comb = len_words_reg;
        }
        else if (addr == REG_CONTROL) {
            read_word_comb = (dma_write_reg ? CTRL_DIR_A2M : 0u);
        }
        else if (addr == REG_STATUS) {
            read_word_comb = (busy_reg ? 1u : 0u) | (done_reg ? 2u : 0u) | (error_reg ? 4u : 0u);
        }
        else if (addr == REG_PRBS_SEED) {
            read_word_comb = prbs_reg;
        }
        else if (addr >= REG_MEM_BASE && addr < REG_MEM_BASE + MEM_WORDS * 4u) {
            mem_index = (addr - REG_MEM_BASE) / 4u;
            read_word_comb = accel_mem_reg[mem_index];
        }
        return read_word_comb;
    }

    _LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        size_t lane;
        uint32_t addr;
        uint32_t beat_base;
        uint32_t mem_index;
        uint32_t word;
        beat_base = (uint32_t)read_addr_reg & ~(DATA_BYTES - 1u);
        read_data_comb = 0;
        for (lane = 0; lane < DATA_BYTES / 4; ++lane) {
            addr = beat_base + lane * 4u;
            if (addr >= 0x100u && addr < 0x120u) {
                addr -= 0x100u;
            }
            word = 0;
            if (addr == REG_SRC_ADDR) {
                word = src_addr_reg;
            }
            else if (addr == REG_DST_ADDR) {
                word = dst_addr_reg;
            }
            else if (addr == REG_LEN_WORDS) {
                word = len_words_reg;
            }
            else if (addr == REG_CONTROL) {
                word = dma_write_reg ? CTRL_DIR_A2M : 0u;
            }
            else if (addr == REG_STATUS) {
                word = (busy_reg ? 1u : 0u) | (done_reg ? 2u : 0u) | (error_reg ? 4u : 0u);
            }
            else if (addr == REG_PRBS_SEED) {
                word = prbs_reg;
            }
            else if (addr >= REG_MEM_BASE && addr < REG_MEM_BASE + MEM_WORDS * 4u) {
                mem_index = (addr - REG_MEM_BASE) / 4u;
                word = accel_mem_reg[mem_index];
            }
            read_data_comb.bits(lane * 32 + 31, lane * 32) = word;
        }
        return read_data_comb;
    }

    _LAZY_COMB(write_word_comb, uint32_t)
        uint32_t lane;
        lane = ((uint32_t)write_addr_reg % DATA_BYTES) / 4u;
        write_word_comb = (uint32_t)axi_in.wdata_in().bits(lane * 32 + 31, lane * 32);
        return write_word_comb;
    }

    _LAZY_COMB(dma_addr_comb, uint32_t)
        if (dma_write_reg) {
            dma_addr_comb = dst_addr_reg + (uint32_t)index_reg * 4u;
        }
        else {
            dma_addr_comb = src_addr_reg + (uint32_t)index_reg * 4u;
        }
        return dma_addr_comb;
    }

    _LAZY_COMB(dma_wdata_comb, uint32_t)
        uint32_t mem_index;
        mem_index = (uint32_t)src_addr_reg + (uint32_t)index_reg;
        if (mem_index < MEM_WORDS) {
            dma_wdata_comb = accel_mem_reg[mem_index];
        }
        else {
            dma_wdata_comb = 0;
        }
        return dma_wdata_comb;
    }

    _LAZY_COMB(dma_wbeat_comb, logic<DATA_WIDTH>)
        uint32_t lane;
        dma_wbeat_comb = 0;
        lane = (dma_addr_comb_func() % DATA_BYTES) / 4u;
        dma_wbeat_comb.bits(lane * 32 + 31, lane * 32) = dma_wdata_comb_func();
        return dma_wbeat_comb;
    }

    _LAZY_COMB(dma_rword_comb, uint32_t)
        uint32_t lane;
        lane = (dma_addr_comb_func() % DATA_BYTES) / 4u;
        return dma_rword_comb = (uint32_t)dma_out.rdata_out().bits(lane * 32 + 31, lane * 32);
    }

    _LAZY_COMB(prbs_next_comb, uint32_t)
        uint32_t x;
        x = prbs_reg;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return prbs_next_comb = x;
    }

public:
    void _assign()
    {
        axi_in.awready_out = _ASSIGN(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = _ASSIGN(write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.bvalid_out = _ASSIGN_REG(write_resp_valid_reg);
        axi_in.bid_out = _ASSIGN_REG(write_id_reg);
        axi_in.arready_out = _ASSIGN(!read_valid_reg);
        axi_in.rvalid_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rdata_out = _ASSIGN_REG(read_data_reg);
        axi_in.rlast_out = _ASSIGN_REG(read_valid_reg);
        axi_in.rid_out = _ASSIGN_REG(read_id_reg);

        dma_out.awvalid_in = _ASSIGN(state_reg == ST_DMA_AW);
        dma_out.awaddr_in = _ASSIGN((u<ADDR_WIDTH>)dma_addr_comb_func());
        dma_out.awid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.wvalid_in = _ASSIGN(state_reg == ST_DMA_W);
        dma_out.wdata_in = _ASSIGN(dma_wbeat_comb_func());
        dma_out.wlast_in = _ASSIGN(state_reg == ST_DMA_W);
        dma_out.bready_in = _ASSIGN(state_reg == ST_DMA_B && dma_out.bvalid_out());
        dma_out.arvalid_in = _ASSIGN(state_reg == ST_DMA_AR);
        dma_out.araddr_in = _ASSIGN((u<ADDR_WIDTH>)dma_addr_comb_func());
        dma_out.arid_in = _ASSIGN((u<ID_WIDTH>)0);
        dma_out.rready_in = _ASSIGN(state_reg == ST_DMA_R && dma_out.rvalid_out());
    }

    void _work(bool reset)
    {
        uint32_t addr;
        uint32_t beat_base;
        uint32_t word;
        uint32_t mem_index;
        logic<DATA_WIDTH> read_data;
        size_t i;

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            addr = (uint32_t)axi_in.araddr_in();
            beat_base = addr & ~(DATA_BYTES - 1u);
            read_data = 0;
            for (i = 0; i < DATA_BYTES / 4; ++i) {
                addr = beat_base + i * 4u;
                if (addr >= 0x100u && addr < 0x120u) {
                    addr -= 0x100u;
                }
                word = 0;
                if (addr == REG_SRC_ADDR) {
                    word = src_addr_reg;
                }
                else if (addr == REG_DST_ADDR) {
                    word = dst_addr_reg;
                }
                else if (addr == REG_LEN_WORDS) {
                    word = len_words_reg;
                }
                else if (addr == REG_CONTROL) {
                    word = dma_write_reg ? CTRL_DIR_A2M : 0u;
                }
                else if (addr == REG_STATUS) {
                    word = (busy_reg ? 1u : 0u) | (done_reg ? 2u : 0u) | (error_reg ? 4u : 0u);
                }
                else if (addr == REG_PRBS_SEED) {
                    word = prbs_reg;
                }
                else if (addr >= REG_MEM_BASE && addr < REG_MEM_BASE + MEM_WORDS * 4u) {
                    mem_index = (addr - REG_MEM_BASE) / 4u;
                    word = accel_mem_reg[mem_index];
                }
                read_data.bits(i * 32 + 31, i * 32) = word;
            }
            read_addr_reg._next = axi_in.araddr_in();
            read_id_reg._next = axi_in.arid_in();
            read_data_reg._next = read_data;
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
            if (addr >= 0x100u && addr < 0x120u) {
                addr -= 0x100u;
            }
            word = write_word_comb_func();
            write_addr_valid_reg._next = false;
            write_resp_valid_reg._next = true;
            if (addr == REG_SRC_ADDR) {
                src_addr_reg._next = word;
            }
            else if (addr == REG_DST_ADDR) {
                dst_addr_reg._next = word;
            }
            else if (addr == REG_LEN_WORDS) {
                len_words_reg._next = word;
            }
            else if (addr == REG_PRBS_SEED) {
                prbs_reg._next = word ? word : 1u;
            }
            else if (addr == REG_CONTROL && (word & CTRL_START) && !busy_reg) {
                done_reg._next = false;
                error_reg._next = false;
                index_reg._next = 0;
                if (word & CTRL_PRBS) {
                    for (i = 0; i < MEM_WORDS; ++i) {
                        accel_mem_reg._next[i] = prbs_next_comb_func() + i;
                    }
                    prbs_reg._next = prbs_next_comb_func();
                    done_reg._next = true;
                }
                else if (len_words_reg == 0 || len_words_reg > MEM_WORDS ||
                         (((word & CTRL_DIR_A2M) != 0) && src_addr_reg + len_words_reg > MEM_WORDS) ||
                         (((word & CTRL_DIR_A2M) == 0) && dst_addr_reg + len_words_reg > MEM_WORDS)) {
                    error_reg._next = true;
                    done_reg._next = true;
                }
                else {
                    dma_write_reg._next = (word & CTRL_DIR_A2M) != 0;
                    busy_reg._next = true;
                    state_reg._next = ((word & CTRL_DIR_A2M) != 0) ? ST_DMA_AW : ST_DMA_AR;
                }
            }
            else if (addr >= REG_MEM_BASE && addr < REG_MEM_BASE + MEM_WORDS * 4u) {
                mem_index = (addr - REG_MEM_BASE) / 4u;
                accel_mem_reg._next[mem_index] = word;
            }
        }
        if (write_resp_valid_reg && axi_in.bready_in()) {
            write_resp_valid_reg._next = false;
        }

        if (state_reg == ST_DMA_AR) {
            if (dma_out.arvalid_in() && dma_out.arready_out()) {
                state_reg._next = ST_DMA_R;
            }
        }
        else if (state_reg == ST_DMA_R) {
            if (dma_out.rvalid_out() && dma_out.rready_in()) {
                accel_mem_reg._next[dst_addr_reg + index_reg] = dma_rword_comb_func();
                if (index_reg + 1 >= len_words_reg) {
                    state_reg._next = ST_DONE;
                }
                else {
                    index_reg._next = index_reg + 1;
                    state_reg._next = ST_DMA_AR;
                }
            }
        }
        else if (state_reg == ST_DMA_AW) {
            if (dma_out.awvalid_in() && dma_out.awready_out()) {
                state_reg._next = ST_DMA_W;
            }
        }
        else if (state_reg == ST_DMA_W) {
            if (dma_out.wvalid_in() && dma_out.wready_out()) {
                state_reg._next = ST_DMA_B;
            }
        }
        else if (state_reg == ST_DMA_B) {
            if (dma_out.bvalid_out() && dma_out.bready_in()) {
                if (index_reg + 1 >= len_words_reg) {
                    state_reg._next = ST_DONE;
                }
                else {
                    index_reg._next = index_reg + 1;
                    state_reg._next = ST_DMA_AW;
                }
            }
        }
        else if (state_reg == ST_DONE) {
            busy_reg._next = false;
            done_reg._next = true;
            state_reg._next = ST_IDLE;
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
            src_addr_reg.clr();
            dst_addr_reg.clr();
            len_words_reg.clr();
            index_reg.clr();
            prbs_reg._next = 1;
            busy_reg.clr();
            done_reg.clr();
            error_reg.clr();
            dma_write_reg.clr();
            state_reg.clr();
            for (i = 0; i < MEM_WORDS; ++i) {
                accel_mem_reg._next[i] = 0;
            }
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        read_addr_reg.strobe(checkpoint_fd);
        read_id_reg.strobe(checkpoint_fd);
        read_valid_reg.strobe(checkpoint_fd);
        read_data_reg.strobe(checkpoint_fd);
        write_addr_reg.strobe(checkpoint_fd);
        write_id_reg.strobe(checkpoint_fd);
        write_addr_valid_reg.strobe(checkpoint_fd);
        write_resp_valid_reg.strobe(checkpoint_fd);
        src_addr_reg.strobe(checkpoint_fd);
        dst_addr_reg.strobe(checkpoint_fd);
        len_words_reg.strobe(checkpoint_fd);
        index_reg.strobe(checkpoint_fd);
        prbs_reg.strobe(checkpoint_fd);
        busy_reg.strobe(checkpoint_fd);
        done_reg.strobe(checkpoint_fd);
        error_reg.strobe(checkpoint_fd);
        dma_write_reg.strobe(checkpoint_fd);
        state_reg.strobe(checkpoint_fd);
        accel_mem_reg.strobe(checkpoint_fd);
    }
};
