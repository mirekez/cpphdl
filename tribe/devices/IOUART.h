#pragma once

#include "cpphdl.h"
#include "Axi4.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256>
class IOUART : public Module
{
public:
    static constexpr uint32_t REG_TXDATA = 0x00;
    static constexpr uint32_t REG_STATUS = 0x04;

    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> axi_in;

    __PORT(bool) uart_valid_out = __VAR(uart_valid_reg);
    __PORT(uint8_t) uart_data_out = __VAR(uart_data_reg);

private:
    reg<u<ADDR_WIDTH>> read_addr_reg;
    reg<u<ID_WIDTH>> read_id_reg;
    reg<u1> read_valid_reg;
    reg<u<ADDR_WIDTH>> write_addr_reg;
    reg<u<ID_WIDTH>> write_id_reg;
    reg<u1> write_addr_valid_reg;
    reg<u1> write_resp_valid_reg;
    reg<u1> uart_valid_reg;
    reg<u8> uart_data_reg;

    // Read data register map: STATUS bit0 is always tx-ready.
    __LAZY_COMB(read_data_comb, logic<DATA_WIDTH>)
        read_data_comb = 0;
        if ((uint32_t)read_addr_reg == REG_STATUS) {
            read_data_comb.bits(31, 0) = 1;
        }
        return read_data_comb;
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
        uart_valid_reg._next = false;

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
            if ((uint32_t)write_addr_reg == REG_TXDATA) {
                uart_data_reg._next = (uint8_t)(uint32_t)axi_in.wdata_in().bits(7, 0);
                uart_valid_reg._next = true;
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
            uart_valid_reg.clr();
            uart_data_reg.clr();
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
        uart_valid_reg.strobe();
        uart_data_reg.strobe();
    }
};
