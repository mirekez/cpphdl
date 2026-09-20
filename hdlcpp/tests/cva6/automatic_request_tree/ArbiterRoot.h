#pragma once
#include "cpphdl.h"
#ifdef CVA6_CONVERSION_METADATA
#include "generated/cf_math_pkg.h"
#include "generated/lzc.h"
#include "generated/rr_arb_tree.h"
#include "generated/ArbiterBench.h"
#else
#include "generated/ArbiterDesign.h"
#endif

class ArbiterRoot : public cpphdl::Module {
public:
    cpphdl::logic<1> reset_n = 0, flush = 0, ready = 0;
    cpphdl::logic<11> requests = 0, grants = 0;
    cpphdl::array<11, cpphdl::logic<103>, true> payload;
    cpphdl::logic<1> valid = 0, last = 0;
    cpphdl::logic<4> index = 0, id = 0;
    cpphdl::logic<2> resp = 0;
    cpphdl::logic<64> data = 0;
    cpphdl::logic<32> user = 0;
    ArbiterBench<> dut;

    void _assign() {
        dut.rst_ni_in = _ASSIGN(reset_n);
        dut.flush_i_in = _ASSIGN(flush);
        dut.ready_in = _ASSIGN(ready);
        dut.requests_in = _ASSIGN(requests);
        dut.payload_in = _ASSIGN(payload);
        dut._assign();
    }
    void _work(bool reset) {
        grants = dut.grants_out();
        valid = dut.valid_out();
        index = dut.index_out();
        id = dut.id_out();
        data = dut.data_out();
        resp = dut.resp_out();
        last = dut.last_out();
        user = dut.user_out();
        dut._work(reset);
    }
    void _strobe() { dut._strobe(); }
};
