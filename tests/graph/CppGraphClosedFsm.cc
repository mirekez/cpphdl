#include <cassert>
#include <cstdio>
#ifdef USE_VERILATOR
#include "VClosedFsm.h"
using Model = VClosedFsm;
#elif defined(USE_NATIVE)
#include "model.h"
using Model = cpphdl_native::Model;
#else
#include "generated/CppGraphClosedFsm.h"
using Model = ClosedFsm;
long _system_clock = 0;
#endif
int main() {
    // The closed-state proof assumes value-initialized state before reset.
    Model model{};
#if !defined(USE_VERILATOR) && !defined(USE_NATIVE)
    cpphdl::logic<1> reset_n = 1, advance = 0;
    model.rst_n_in = _ASSIGN(reset_n); model.advance_in = _ASSIGN(advance);
    model._assign();
#endif
    unsigned expected = 0;
    for (unsigned sample = 0; sample < 10000; ++sample) {
        // Exercise initialization before the first reset as well as later resets.
        bool reset = sample % 79 == 17, go = sample % 3 != 0;
        expected = reset ? 0 : go ? (expected + 1) % 5 : expected;
#ifdef USE_VERILATOR
        model.rst_n = !reset; model.advance = go;
        model.clk = 0; model.eval(); model.clk = 1; model.eval();
        unsigned actual = model.state;
#elif defined(USE_NATIVE)
        model.rst_n[0] = !reset; model.advance[0] = go;
        model.step(); model.eval(false);
        unsigned actual = model.state[0];
#else
        reset_n = !reset; advance = go; ++_system_clock;
        model._work(false); model._strobe(); ++_system_clock;
        unsigned actual = uint64_t(model.state_out());
#endif
        assert(actual == expected);
        std::printf("%u\n", actual);
    }
}
