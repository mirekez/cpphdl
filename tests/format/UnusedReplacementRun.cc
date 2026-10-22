#include "UnusedReplacement.h"
#include <cstdio>
#ifdef REPLACEMENT_RTL
#include "VBbbbbb.h"
#endif

long _system_clock = 0;

class ReplacementHarness : public Module {
public:
    Bbbbbb dut;
    logic<REPLACEMENT_WIDTH> value = 0;

    void _assign() { dut.value_in = _ASSIGN_REG(value); dut._assign(); }
};

int main() {
    ReplacementHarness harness;
#ifdef REPLACEMENT_RTL
    VBbbbbb rtl;
#endif
    harness._assign();
    for (unsigned i = 0; i != (1u << REPLACEMENT_WIDTH); ++i) {
        harness.value = i;
#ifdef REPLACEMENT_RTL
        rtl.clk = 0;
        rtl.reset = 0;
        rtl.value_in = i;
        rtl.eval();
        if (rtl.value_out != i) return 1;
#endif
        if (uint64_t(harness.dut.value_out()) != i) return 2;
        ++_system_clock;
    }
    std::puts("Replacement passthrough passed");
}
