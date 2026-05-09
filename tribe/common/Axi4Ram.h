#pragma once

#include "cpphdl.h"
#include "Axi4.h"
#include "Memory.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256, size_t DEPTH = 2048>
class Axi4Ram : public Module
{
    static constexpr uint64_t FULL_MASK = (DATA_WIDTH / 8 >= 64) ? ~0ull : ((1ull << (DATA_WIDTH / 8)) - 1);
public:
    Memory<DATA_WIDTH / 8, DEPTH, true, 0> ram;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;

    bool debugen_in;

private:
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;

public:
    void _assign()
    {
        axi_in.awready_out = __EXPR(!write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.wready_out = __EXPR(write_addr_valid_reg && !write_resp_valid_reg);
        axi_in.bvalid_out = __VAR(write_resp_valid_reg);
        axi_in.bid_out = __VAR(write_id_reg);
        axi_in.arready_out = __EXPR(!read_valid_reg);
        axi_in.rvalid_out = __VAR(read_valid_reg);
        axi_in.rdata_out = ram.read0_data_out;
        axi_in.rlast_out = __VAR(read_valid_reg);
        axi_in.rid_out = __VAR(read_id_reg);

        ram.addr0_in = __EXPR((u<clog2(DEPTH)>)(read_valid_reg ? (uint32_t)read_addr_reg / (DATA_WIDTH / 8) : axi_in.araddr_in() / (DATA_WIDTH / 8)));
        ram.read0_in = __EXPR(axi_in.arvalid_in() && axi_in.arready_out());
        ram.write0_in = __EXPR(false);
        ram.write0_data_in = __EXPR(logic<DATA_WIDTH>(0));
        ram.write0_mask_in = __EXPR(logic<DATA_WIDTH / 8>(0));

        ram.addr1_in = __EXPR((u<clog2(DEPTH)>)((uint32_t)write_addr_reg / (DATA_WIDTH / 8)));
        ram.read1_in = __EXPR(false);
        ram.write1_in = __EXPR(axi_in.wvalid_in() && axi_in.wready_out());
        ram.write1_data_in = axi_in.wdata_in;
        ram.write1_mask_in = __EXPR((logic<DATA_WIDTH / 8>)FULL_MASK);
        ram.debugen_in = debugen_in;
        ram.__inst_name = __inst_name + "/ram";
        ram._assign();
    }

    void _work(bool reset)
    {
        ram._work(reset);

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
            write_addr_valid_reg._next = false;
            write_resp_valid_reg._next = true;
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
        }
    }

    void _strobe()
    {
        ram._strobe();
        read_addr_reg.strobe();
        read_id_reg.strobe();
        read_valid_reg.strobe();
        write_addr_reg.strobe();
        write_id_reg.strobe();
        write_addr_valid_reg.strobe();
        write_resp_valid_reg.strobe();
    }
};
