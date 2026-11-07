#include "model.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>

// Independent FP16 widening oracle, including signed zeros, subnormals,
// infinities and NaN payloads. No host float conversions or helper reuse.
static uint32_t widen(uint32_t bits) {
    uint32_t sign = (bits & 0x8000u) << 16;
    unsigned exponent = (bits >> 10) & 31u;
    uint32_t fraction = bits & 1023u;
    if (exponent == 31) return sign | 0x7f800000u | (fraction << 13);
    if (exponent) return sign | ((exponent + 112u) << 23) | (fraction << 13);
    if (!fraction) return sign;
    unsigned shift = 0;
    while (!(fraction & 1024u)) { fraction <<= 1; ++shift; }
    return sign | ((113u - shift) << 23) | ((fraction & 1023u) << 13);
}

int main() {
    cpphdl_native::Model model;
    std::deque<uint64_t> expected;
    unsigned completed = 0;
    auto tick = [&] {
        model.clk[0] = 1; model.step();
        model.clk[0] = 0; model.step();
    };
    model.reset[0] = 1; model.clk[0] = 0; model.step(); tick();
    model.reset[0] = 0;
    for (unsigned cycle = 0; cycle < 4096; ++cycle) {
        bool reset = cycle == 1801;
        unsigned a = cycle < 2048 ? cycle : (cycle * 977u) & 65535u;
        unsigned b = a ^ 0xffffu;
        bool mix = cycle % 3 == 0;
        model.reset[0] = reset;
        model.operation_in[0] = mix;
        model.value_in[0] = a; model.index_in[0] = b;
        model.command_valid_in[0] = cycle < 4000 && cycle % 7 != 0;
        model.response_ready_in[0] = cycle % 11 > 2;
        model.step();
        if (reset) expected.clear();
        else {
            if (model.fault_out[0]) throw std::runtime_error("pipeline fault");
            if (model.response_valid_out[0]) {
                uint64_t actual = uint64_t(model.result_out[0]) | (uint64_t(model.result_out[1]) << 32);
                if (expected.empty() || actual != expected.front())
                    throw std::runtime_error("OpenMP alias/FP16 result mismatch");
                if (model.response_ready_in[0]) { expected.pop_front(); ++completed; }
            }
            if (model.command_valid_in[0] && model.command_ready_out[0]) {
                uint32_t lo = mix ? (a ^ 0x12345678u) + 19u : widen(a);
                uint32_t hi = mix ? (b ^ 0x12345678u) + 19u : widen(b);
                expected.push_back(uint64_t(lo) | (uint64_t(hi) << 32));
            }
        }
        tick();
    }
    if (completed < 2000 || !expected.empty()) throw std::runtime_error("incomplete pipeline test");
    std::printf("PASS OpenMP type aliases and FP16 converter: %u transactions, stalls and reset\n", completed);
}
