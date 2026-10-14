#include <cstdint>
#include <cstdio>
#ifdef UNDRIVEN_VERILATOR
#include "VUndrivenVariable.h"
#elif defined(UNDRIVEN_GRAPH)
#include "model.h"
#else
#include "generated/UndrivenVariable.h"
long _system_clock = 0;
#endif

int main() {
#ifdef UNDRIVEN_VERILATOR
    VUndrivenVariable model;
#elif defined(UNDRIVEN_GRAPH)
    cpphdl_native::Model model;
#else
    UndrivenVariable<ENABLE_VALUE> model{};
    cpphdl::logic<8> data;
    cpphdl::logic<1> reset;
    model.data_i_in = _ASSIGN(data);
    model.rst_ni = _ASSIGN(reset);
    model._assign();
#endif
    for (unsigned sample = 0; sample < 4096; ++sample) {
        auto input = uint8_t(sample * 37);
        auto resetN = sample % 13 != 0;
#ifdef UNDRIVEN_VERILATOR
        model.data_i = input; model.rst_ni = resetN;
        model.clk_i = 0; model.eval(); model.clk_i = 1; model.eval();
        auto actual = uint32_t(model.result_o);
        auto aggregate = uint32_t(model.aggregate_o);
#elif defined(UNDRIVEN_GRAPH)
        model.data_i[0] = input; model.rst_ni[0] = resetN;
        model.work_reset[0] = !resetN; model.step();
        auto actual = uint32_t(model.result_o[0]);
        auto aggregate = uint32_t(model.aggregate_o[0]);
#else
        data = input; reset = resetN;
        ++_system_clock;
        model._work(!resetN); model._strobe();
        ++_system_clock;
        auto actual = uint32_t(uint64_t(model.result_o_out()));
        auto aggregate = uint32_t(uint64_t(model.aggregate_o_out()));
#endif
        auto expected = (uint32_t(ENABLE_VALUE && resetN ? input : 0) << 24) |
            (uint32_t(uint8_t(input + 1)) << 8) | uint8_t(input ^ 0xf0);
        if (actual != expected || aggregate != input) return 1;
    }
    std::puts("driverless SV variables: 4096 oracle samples PASS");
}
