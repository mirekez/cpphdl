#pragma once

#include "cpphdl.h"
#include <array>
#include <tuple>
#include <print>
#include <type_traits>
#include <utility>

// Observation only: the DUT and every computation come from converted RTL.
// Match cpphdl's access shim instead of copying a private method into a test.
#define private public
#include "generated/cf_math_pkg.h"
#include "generated/lzc.h"
#include "generated/rr_arb_tree.h"
#undef private

#ifndef REQUEST_TREE_INPUTS
#define REQUEST_TREE_INPUTS 16
#endif

class RequestTreeRoot : public cpphdl::Module {
public:
    static constexpr unsigned Inputs = REQUEST_TREE_INPUTS;
    static constexpr unsigned TreeBits = (1u << cpphdl::clog2(Inputs)) - 1;
    static_assert(Inputs >= 2 && Inputs <= 16);
    cpphdl::logic<Inputs> requests = 0;
    cpphdl::logic<TreeBits> observed = 0;
    rr_arb_tree<Inputs, 1, cpphdl::logic<1>, 1> dut;

    void _assign() {
        dut.req_i_in = _ASSIGN(requests);
        dut._assign();
    }
    void _work(bool) { observed = dut.req_nodes_comb_func(); }
    void _strobe() {}
};
