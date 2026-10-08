#pragma once
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VScalarMath.h"
extern "C" unsigned long long llm_model_divide(unsigned long long a, unsigned long long b) {
    return b ? uint64_t(integer_llm::divide(int64_t(a), int64_t(b))) : 0;
}
extern "C" unsigned long long llm_model_exp(unsigned long long a) {
    // Other mux branches can evaluate even when this operation is not selected.
    return int64_t(a) <= 0 ? uint64_t(integer_llm::exp_negative(int64_t(a))) : 0;
}
extern "C" unsigned long long llm_model_inverse_sqrt(unsigned long long a) {
    return uint64_t(integer_llm::inverse_sqrt(int64_t(a)));
}
extern "C" unsigned long long llm_model_silu(unsigned long long a) {
    return uint64_t(integer_llm::silu(int64_t(a)));
}
#endif
long _system_clock = 0;

int main() {
    using namespace integer_llm;
#ifdef VERILATOR
    VScalarMath dut;
#else
    ::ScalarMath dut;
#endif
    std::mt19937_64 random(567);
    std::deque<uint64_t> expected;
    bool valid = false, ready = true;
    uint64_t lhs = 0, rhs = one, operation = 0;
    unsigned sent = 0, got = 0;
#ifndef VERILATOR
    dut.valid_in = _ASSIGN(valid); dut.ready_in = _ASSIGN(ready);
    dut.lhs_in = _ASSIGN(lhs); dut.rhs_in = _ASSIGN(rhs);
    dut.operation_in = _ASSIGN(operation); dut._assign();
#endif
    for (unsigned cycle = 0; cycle < 100000 && got < 1200; ++cycle) {
        bool reset = cycle < 2;
        if (!valid && sent < 1200 && !reset) {
            operation = sent % 6;
            lhs = uint64_t(int64_t(random() % (8 * one)) - 4 * one);
            rhs = uint64_t(int64_t(random() % (4 * one)) + one / 4);
            if (operation == 3) lhs = uint64_t(-int64_t(random() % (40 * one)));
            if (operation == 4) lhs = random() % (8 * one) + one / 16;
            if (sent < 24 && operation < 2) {
                lhs = sent < 12 ? uint64_t(INT64_MIN) : uint64_t(INT64_MAX);
                rhs = sent % 12 < 6 ? uint64_t(-1) : uint64_t(INT64_MAX);
            }
            valid = true;
        }
        ready = random() % 4 != 0;
#ifdef VERILATOR
        dut.clk = 0; dut.reset = reset;
        dut.valid_in = valid; dut.ready_in = ready;
        dut.lhs_in = lhs; dut.rhs_in = rhs; dut.operation_in = operation; dut.eval();
        bool accepted = !reset && valid && dut.ready_out;
        bool output = !reset && dut.valid_out;
        uint64_t result = dut.result_out;
#else
        bool accepted = !reset && valid && dut.ready_out();
        bool output = !reset && dut.valid_out();
        uint64_t result = output ? dut.result_out() : 0;
#endif
        if (output) {
            if (expected.empty() || expected.front() != result)
                throw std::runtime_error("Q48 scalar arithmetic mismatch at result " + std::to_string(got));
            if (ready) { expected.pop_front(); ++got; }
        }
        if (accepted) {
            expected.push_back(integer_llm::ScalarMath{}.command(lhs, rhs, operation));
            ++sent;
        }
#ifdef VERILATOR
        dut.clk = 1; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        if (accepted) valid = false;
        ++_system_clock;
    }
    if (got != 1200) throw std::runtime_error("scalar math timeout");
    std::puts("PASS: 1200 add/mul/divide/exp/inverse-sqrt/SiLU results with backpressure");
}
