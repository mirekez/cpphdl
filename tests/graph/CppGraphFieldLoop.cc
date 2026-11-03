#include "cpphdl.h"

class GraphFieldLoop : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<4>) stop_in;
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(cpphdl::logic<8>) index_out = _ASSIGN(cpphdl::logic<8>(index));
    _PORT(cpphdl::logic<8>) sum_out = _ASSIGN(cpphdl::logic<8>(sum));
    _PORT(cpphdl::logic<8>) wrapped_out = _ASSIGN(cpphdl::logic<8>(uint64_t(wrapped)));
    unsigned index, sum;
    cpphdl::u32 wrapped;
    void _work(bool reset) {
        if (reset) { index = 99; sum = 0; wrapped = 99; }
        else if (enable_in()) {
            sum = 0;
            // Like Verilog's module-scoped integer, index must retain the
            // breaking iteration or the terminal bound, and hold when disabled.
            for (index = 0; index < 8; ++index) {
                if (index == uint64_t(stop_in())) break;
                if (index & 1) continue;
                sum += index;
            }
            for (wrapped = 0; uint64_t(wrapped) < 8;
#ifdef CPP_GRAPH_FIELD_LOOP_EFFECT
                 wrapped = uint64_t(wrapped) + (++sum)
#else
                 wrapped++
#endif
            ) {
                if (uint64_t(wrapped) == uint64_t(stop_in())) break;
                if (uint64_t(wrapped) & 1) continue;
            }
        }
    }
    void _strobe() {}
};
GraphFieldLoop cpphdl_top;

#ifdef CPP_GRAPH_FIELD_LOOP_RUN
#include "model.h"
#include <cassert>
#include <cstdio>
long _system_clock = 0;
int main() {
    cpphdl_native::Model model;
    cpphdl::logic<4> stop;
    cpphdl::logic<1> enable;
    cpphdl_top.stop_in = _ASSIGN(stop);
    cpphdl_top.enable_in = _ASSIGN(enable);
    unsigned expectedIndex = 0, expectedSum = 0;
    for (unsigned sample = 0; sample < 512; ++sample) {
        stop = sample % 16;
        enable = (sample / 16) % 2;
        bool reset = sample % 79 == 0;
        model.stop[0] = uint64_t(stop);
        model.enable[0] = uint64_t(enable);
        model.work_reset[0] = reset;
        if (reset) { expectedIndex = 99; expectedSum = 0; }
        else if (uint64_t(enable)) {
            expectedIndex = sample % 16 < 8 ? sample % 16 : 8;
            expectedSum = 0;
            for (unsigned i = 0; i < expectedIndex; i += 2) expectedSum += i;
        }
        cpphdl_top._work(reset);
        cpphdl_top._strobe();
        ++_system_clock;
        model.step(); model.eval(false);
        assert(uint64_t(cpphdl_top.index_out()) == expectedIndex);
        assert(uint64_t(cpphdl_top.sum_out()) == expectedSum);
        assert(model.index[0] == expectedIndex);
        assert(model.sum[0] == expectedSum);
        assert(uint64_t(cpphdl_top.wrapped_out()) == expectedIndex);
        assert(model.wrapped[0] == expectedIndex);
    }
    std::puts("ordinary C++ graph: field loop counter, break, continue and hold match 512 samples");
}
#endif
