#pragma once

#include "cpphdl.h"
#include "Axi4.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256>
class CLINT : public Module
{
    static constexpr uint32_t REG_MSIP = 0x0000;
    static constexpr uint32_t REG_MTIMECMP_LO = 0x4000;
    static constexpr uint32_t REG_MTIMECMP_HI = 0x4004;
    static constexpr uint32_t REG_MTIME_LO = 0xBFF8;
    static constexpr uint32_t REG_MTIME_HI = 0xBFFC;
    static constexpr uint32_t DATA_BYTES = DATA_WIDTH / 8;

public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;

    __PORT(bool) msip_out = __VAR(msip_comb_func());
    __PORT(bool) mtip_out = __VAR(mtip_comb_func());

private:
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;
    reg<u32> msip_reg;
    reg<u64> mtime_reg;
    reg<u64> mtimecmp_reg;

    // AXI data beat lane corresponding to the saved local register address.
    __LAZY_COMB(read_word_lane_comb, uint32_t)
        return read_word_lane_comb = ((uint32_t)read_addr_reg % DATA_BYTES) / 4u;
    }

    // Value of the addressed CLINT register before it is inserted into its AXI lane.
    __LAZY_COMB(read_word_comb, uint32_t)
        uint32_t addr;
        addr = (uint32_t)read_addr_reg;
        read_word_comb = 0;
        if (addr == REG_MSIP) {
            read_word_comb = msip_reg;
        }
        else if (addr == REG_MTIMECMP_LO) {
            read_word_comb = (uint32_t)mtimecmp_reg;
        }
        else if (addr == REG_MTIMECMP_HI) {
            read_word_comb = (uint32_t)((uint64_t)mtimecmp_reg >> 32);
        }
        else if (addr == REG_MTIME_LO) {
            read_word_comb = (uint32_t)mtime_reg;
        }
        else if (addr == REG_MTIME_HI) {
            read_word_comb = (uint32_t)((uint64_t)mtime_reg >> 32);
        }
        return read_word_comb;
    }

    // Broadcast 32-bit CLINT registers on every lane. The current simple AXI
    // subset has no byte strobes, and this keeps MMIO reads independent of the
    // upstream bus width/lane placement.
    __LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        size_t lane;
        read_data_comb = 0;
        for (lane = 0; lane < DATA_BYTES / 4; ++lane) {
            read_data_comb.bits(lane * 32 + 31, lane * 32) = read_word_comb_func();
        }
        return read_data_comb;
    }

    // Accept the 32-bit MMIO write word from any lane. L2 emits only one active
    // word for uncached stores; OR-reducing lanes also preserves all-zero writes.
    __LAZY_COMB(write_word_comb, uint32_t)
        size_t lane;
        write_word_comb = 0;
        for (lane = 0; lane < DATA_BYTES / 4; ++lane) {
            write_word_comb |= (uint32_t)axi_in.wdata_in().bits(lane * 32 + 31, lane * 32);
        }
        return write_word_comb;
    }

    __LAZY_COMB(msip_comb, bool)
        return msip_comb = (msip_reg & 1u) != 0;
    }

    __LAZY_COMB(mtip_comb, bool)
        return mtip_comb = mtime_reg >= mtimecmp_reg;
    }

public:
    void _assign()
    {
        axi_in.awready_out = __EXPR(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = __EXPR(write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.bvalid_out = __VAR(write_resp_valid_reg);
        axi_in.bid_out = __VAR(write_id_reg);
        axi_in.arready_out = __EXPR(!read_valid_reg);
        axi_in.rvalid_out = __VAR(read_valid_reg);
        axi_in.rdata_out = __VAR(read_data_comb_func());
        axi_in.rlast_out = __VAR(read_valid_reg);
        axi_in.rid_out = __VAR(read_id_reg);
    }

    void _work(bool reset)
    {
        uint32_t addr;
        uint32_t word;
        mtime_reg._next = (uint64_t)mtime_reg + 1u;

        if (axi_in.arvalid_in() && axi_in.arready_out()) {
            read_addr_reg._next = axi_in.araddr_in();
            read_id_reg._next = axi_in.arid_in();
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
            if (addr == REG_MSIP) {
                msip_reg._next = word & 1u;
            }
            else if (addr == REG_MTIMECMP_LO) {
                mtimecmp_reg._next = ((uint64_t)mtimecmp_reg & 0xffffffff00000000ull) | (uint64_t)word;
            }
            else if (addr == REG_MTIMECMP_HI) {
                mtimecmp_reg._next = ((uint64_t)word << 32) | (uint64_t)(uint32_t)mtimecmp_reg;
            }
            else if (addr == REG_MTIME_LO) {
                mtime_reg._next = ((uint64_t)mtime_reg & 0xffffffff00000000ull) | (uint64_t)word;
            }
            else if (addr == REG_MTIME_HI) {
                mtime_reg._next = ((uint64_t)word << 32) | (uint64_t)(uint32_t)mtime_reg;
            }
        }
        if (write_resp_valid_reg && axi_in.bready_in()) {
            write_resp_valid_reg._next = false;
        }

        if (reset) {
            read_addr_reg.clr();
            read_id_reg.clr();
            read_valid_reg.clr();
            write_addr_reg.clr();
            write_id_reg.clr();
            write_addr_valid_reg.clr();
            write_resp_valid_reg.clr();
            msip_reg.clr();
            mtime_reg.clr();
            mtimecmp_reg._next = ~0ull;
        }
    }

    void _strobe()
    {
        read_addr_reg.strobe();
        read_id_reg.strobe();
        read_valid_reg.strobe();
        write_addr_reg.strobe();
        write_id_reg.strobe();
        write_addr_valid_reg.strobe();
        write_resp_valid_reg.strobe();
        msip_reg.strobe();
        mtime_reg.strobe();
        mtimecmp_reg.strobe();
    }
};
