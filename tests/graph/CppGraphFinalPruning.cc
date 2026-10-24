#include "cpphdl.h"
#include <cstdlib>
#ifndef PRUNE_EFFECT
#define PRUNE_EFFECT 0
#endif
#ifndef PRUNE_MEMORY
#define PRUNE_MEMORY 0
#endif
#ifndef PRUNE_OBSERVE
#define PRUNE_OBSERVE 0
#endif

class HiddenDiagnostic : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(cpphdl::logic<8>) data_in;
    cpphdl::reg<cpphdl::logic<8>> state;
    cpphdl::logic<8> held;
    cpphdl::memory<cpphdl::logic<8>, 1, 16> memory;
    cpphdl::logic<8>& comb() {
        if (enable_in()) held = data_in();
        return held;
    }
    void _work(bool reset) {
        state._next = state;
        if (reset) state._next = 0;
        else state._next = comb();
        if constexpr (PRUNE_EFFECT == 1) (void)::random();
        if constexpr (PRUNE_EFFECT == 2) if (state[0]) (void)::random();
        if constexpr (PRUNE_MEMORY == 1) memory.pending(0) = uint64_t(state);
        if constexpr (PRUNE_MEMORY == 2) (void)memory[uint64_t(state)];
    }
    void _strobe() {
        state.strobe();
        if constexpr (PRUNE_MEMORY == 1) memory.apply();
    }
};

class FinalPruningModel : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<1>) enable_in;
    _PORT(cpphdl::logic<8>) data_in;
    _PORT(cpphdl::logic<8>) result_out = _ASSIGN_REG(total);
#if PRUNE_OBSERVE
    _PORT(cpphdl::logic<8>) diagnostic_out = _ASSIGN_REG(hidden.state);
#endif
    cpphdl::reg<cpphdl::logic<8>> total;
    HiddenDiagnostic hidden;
    void _assign() {
        hidden.enable_in = _ASSIGN(enable_in());
        hidden.data_in = _ASSIGN(data_in());
    }
    void _work(bool reset) {
        total._next = reset ? cpphdl::logic<8>(0) : cpphdl::logic<8>(total + data_in());
        hidden._work(reset);
    }
    void _strobe() { total.strobe(); hidden._strobe(); }
};

extern FinalPruningModel cpphdl_top;

#ifdef FINAL_PRUNING_RUN
#include <cassert>
#include <cstdio>
#ifdef FINAL_PRUNING_GRAPH
#include "model.h"
#else
long _system_clock = 0;
#endif
static unsigned calls;
extern "C" long random() noexcept { ++calls; return 17; }

int main() {
#ifdef FINAL_PRUNING_GRAPH
    cpphdl_native::Model model;
#else
    static FinalPruningModel model;
    cpphdl::logic<1> enable;
    cpphdl::logic<8> data;
    model.enable_in = _ASSIGN(enable);
    model.data_in = _ASSIGN(data);
    model._assign();
#endif
    unsigned expected = 0;
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const bool reset = sample % 101 == 0;
        const unsigned value = (sample * 31 + 7) & 255;
        expected = reset ? 0 : (expected + value) & 255;
#ifdef FINAL_PRUNING_GRAPH
        model.work_reset[0] = reset;
        model.enable[0] = sample & 1;
        model.data[0] = value;
        model.eval(false);
        model.eval(false);
        assert(calls == (PRUNE_EFFECT == 1 ? sample : 0));
        model.step();
        assert(model.result[0] == expected);
#else
        enable = sample & 1;
        data = value;
        ++_system_clock;
        model._work(reset);
        model._strobe();
        assert(uint64_t(model.result_out()) == expected);
#endif
        assert(calls == (PRUNE_EFFECT == 1 ? sample + 1 : 0));
    }
    std::puts("final native pruning: 4096 outputs and callback counts PASS");
}
#endif
