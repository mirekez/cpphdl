#pragma once
#include "cpphdl.h"
#include "generated/config_pkg.h"
#include "generated/ariane_regfile_ff.h"

// Configuration only: the decoder, state, and read mux are converted RTL.
inline constexpr auto RegfileConfig = [] {
    config_pkg::cva6_cfg_t value{};
    value.NrCommitPorts = 2;
    return value;
}();
using RegfileDut = ariane_regfile<RegfileConfig, 32, 2, 1>;

class RegfileRoot : public cpphdl::Module {
public:
    cpphdl::logic<1> reset_n = 0;
    cpphdl::array<2, cpphdl::logic<5>, true> read_addresses, write_addresses;
    cpphdl::array<2, cpphdl::logic<32>, true> write_data, read_data;
    cpphdl::logic<2> enables = 0;
    RegfileDut dut;
    void _assign() {
        dut.rst_ni_in = _ASSIGN(reset_n);
        dut.test_en_i_in = _ASSIGN(cpphdl::logic<1>(0));
        dut.raddr_i_in = _ASSIGN(read_addresses);
        dut.waddr_i_in = _ASSIGN(write_addresses);
        dut.wdata_i_in = _ASSIGN(write_data);
        dut.we_i_in = _ASSIGN(enables);
        dut._assign();
    }
    void _work(bool reset) {
        read_data = dut.rdata_o_out();
        dut._work(reset);
    }
    void _strobe() { dut._strobe(); }
};
