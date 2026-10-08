#include "SwitchEffects.cc"
#include <cstdio>
#ifdef TEST_GRAPH
#include "model.h"
#endif
long _system_clock = 0;
unsigned calls = 0;
extern "C" long __wrap_random() { return ++calls; }
class Driver : public Module {
public:
    uint32_t selector = 0;
    SwitchEffects dut;
    void _assign() {
        dut.selector_in = _ASSIGN(selector);
        dut._assign();
    }
};
int main() {
    Driver driver;
    driver._assign();
#ifdef TEST_GRAPH
    cpphdl_native::Model model;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
        const unsigned selector = i % 3;
        const unsigned expected_calls = selector == 0 ? 1 : selector == 1 ? 0 : 2;
        const unsigned expected_value = selector == 0 ? 1 : selector == 1 ? 7 : 3;
        driver.selector = selector;
        calls = 0;
        ++_system_clock;
        driver.dut._work(false);
        driver.dut._strobe();
        if (calls != expected_calls || driver.dut.result_out() != expected_value) {
            std::fprintf(stderr, "native selector %u calls %u expected %u, value %u expected %u\n",
                         selector, calls, expected_calls, driver.dut.result_out(), expected_value);
            return 1;
        }
#ifdef TEST_GRAPH
        calls = 0;
        model.selector[0] = selector;
        model.eval();
        if (calls) return 2;
        model.step();
        if (calls != expected_calls || model.result[0] != expected_value) {
            std::fprintf(stderr, "graph selector %u calls %u expected %u, value %u expected %u\n",
                         selector, calls, expected_calls, model.result[0], expected_value);
            return 3;
        }
#endif
    }
    std::puts("1000 switch host-effect checks passed");
}
