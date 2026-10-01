#include "cpphdl.h"

class SynthAdder : public cpphdl::Module {
public:
    _PORT(uint16_t) a_in;
    _PORT(uint16_t) b_in;
    _PORT(bool) en_in;
    _PORT(cpphdl::u<16>) result_out = _ASSIGN_REG(result);
    cpphdl::reg<cpphdl::u<16>> result;
    void _work(bool reset) {
        result._next = result;
        if (en_in()) result._next = a_in() + b_in();
        if (reset) result._next = 0;
    }
    void _strobe() { result.strobe(); }
};

class SynthPipeline : public cpphdl::Module {
    SynthAdder adder;
public:
    _PORT(uint16_t) a_in;
    _PORT(uint16_t) b_in;
    _PORT(bool) en_in;
    _PORT(cpphdl::u<16>) first_out = _ASSIGN(adder.result_out());
    _PORT(cpphdl::u<16>) delayed_out = _ASSIGN_REG(delayed);
    cpphdl::reg<cpphdl::u<16>> delayed;
    void _assign() {
        adder.a_in = _ASSIGN(a_in());
        adder.b_in = _ASSIGN(b_in());
        adder.en_in = _ASSIGN(en_in());
    }
    void _work(bool reset) {
        adder._work(reset);
        delayed._next = delayed;
        if (en_in()) delayed._next = adder.result_out();
        if (reset) delayed._next = 0;
    }
    void _strobe() {
        adder._strobe();
        delayed.strobe();
    }
#ifdef SYNTH_BAD_CLOCK
    void _work_other_clock(bool reset) { delayed._next = reset ? 0 : 1; }
#endif
};
SynthPipeline cpphdl_top;

#ifdef SYNTH_PIPELINE_RUN
#include <cstdio>
#include <cstdlib>
#ifdef SYNTH_PIPELINE_GRAPH
#include "model.h"
#endif
#ifdef SYNTH_PIPELINE_VERILATOR
#include "VSynthPipeline.h"
#endif
long _system_clock = 0;
int main() {
    uint16_t a = 0, b = 0;
    bool enable = false;
    uint16_t first = 0, delayed = 0;
#ifdef SYNTH_PIPELINE_GRAPH
    cpphdl_native::Model dut;
#endif
#ifdef SYNTH_PIPELINE_VERILATOR
    VSynthPipeline dut;
#endif
    cpphdl_top.a_in = _ASSIGN(a);
    cpphdl_top.b_in = _ASSIGN(b);
    cpphdl_top.en_in = _ASSIGN(enable);
    cpphdl_top._assign();
    for (unsigned sample = 0; sample < 4096; ++sample) {
        a = sample * 11939; b = sample * 23117;
        enable = sample % 5 != 0;
        bool reset = sample % 101 == 0;
        if (enable) { delayed = first; first = uint16_t(a + b); }
        if (reset) { first = 0; delayed = 0; }
#ifdef SYNTH_PIPELINE_GRAPH
        dut.a[0] = a; dut.b[0] = b; dut.en[0] = enable; dut.work_reset[0] = reset;
        dut.step();
        if (dut.first[0] != first || dut.delayed[0] != delayed) return 1;
#endif
#ifdef SYNTH_PIPELINE_VERILATOR
        dut.clk = 0; dut.a = a; dut.b = b; dut.en = enable; dut.work_reset = reset;
        dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
        if (dut.first != first || dut.delayed != delayed) {
            std::fprintf(stderr, "pipeline gates mismatch at %u\n", sample);
            return 1;
        }
#endif
        cpphdl_top._work(reset);
        cpphdl_top._strobe();
        // first_out is a cached value binding used only after the transaction.
        if (uint16_t(cpphdl_top.first_out()) != first || uint16_t(cpphdl_top.delayed_out()) != delayed) {
            std::fprintf(stderr, "pipeline native mismatch at %u\n", sample);
            return 1;
        }
        ++_system_clock;
    }
    std::puts("pipeline: 4096 cycles; hierarchy, simultaneous state, reset and enable passed");
}
#endif
