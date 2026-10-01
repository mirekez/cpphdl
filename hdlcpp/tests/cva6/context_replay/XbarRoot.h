#pragma once
#include "cpphdl.h"
#include "generated/axi_pkg.h"
#include "generated/ariane_soc_pkg.h"
#include "generated/axi_intf.h"
#include "generated/cf_math_pkg.h"
#include "generated/lzc.h"
#include "generated/rr_arb_tree.h"
#include "generated/delta_counter.h"
#include "generated/counter.h"
#include "generated/spill_register_flushable.h"
#include "generated/spill_register.h"
#include "generated/fifo_v3.h"
#include "generated/fifo_v2.h"
#include "generated/addr_decode.h"
#include "generated/stream_register.h"
#include "generated/axi_id_prepend.h"
#include "generated/axi_atop_filter.h"
#include "generated/axi_err_slv.h"
#include "generated/axi_mux.h"
#include "generated/axi_demux.h"
#include "generated/axi_xbar.h"
#include "generated/XbarBench.h"

class XbarRoot : public cpphdl::Module {
public:
    cpphdl::logic<1> reset_n = 0;
    cpphdl::array<2, cpphdl::logic<374>, true> requests;
    cpphdl::array<10, cpphdl::logic<148>, true> responses;
    cpphdl::array<2, cpphdl::logic<146>, true> slave_outputs;
    cpphdl::array<10, cpphdl::logic<376>, true> master_outputs;
    XbarBench<> dut;
    void _assign() {
        dut.rst_ni_in = _ASSIGN(reset_n);
        dut.requests_in = _ASSIGN(requests);
        dut.responses_in = _ASSIGN(responses);
        dut._assign();
    }
    void _work(bool reset) {
        slave_outputs = dut.slave_outputs_out();
        master_outputs = dut.master_outputs_out();
        dut._work(reset);
    }
    void _strobe() { dut._strobe(); }
};
