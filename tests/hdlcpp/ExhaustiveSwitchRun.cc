#include <cassert>
#include <cstdio>
#if defined(SWITCH_GRAPH)
#include "model.h"
#elif defined(SWITCH_RTL)
#include "VExhaustiveSwitch.h"
#else
#include "generated/ExhaustiveSwitch.h"
long _system_clock = 0;
#endif

int main() {
#if defined(SWITCH_GRAPH)
    cpphdl_native::Model model;
#elif defined(SWITCH_RTL)
    VExhaustiveSwitch model;
#else
    ExhaustiveSwitch model;
    cpphdl::logic<4> selector;
    cpphdl::logic<8> data;
    model.selector_in = _ASSIGN(selector);
    model.data_in = _ASSIGN(data);
    model._assign();
#endif
    constexpr unsigned sparseMasks[] = {0x1a, 0x3b, 0x9c, 0xbd};
    for (unsigned sample = 0; sample < 4096; ++sample) {
        const unsigned select = sample & 15;
        const unsigned value = sample >> 4;
        const unsigned sparseIndex = ((select >> 1) & 1) | ((select >> 2) & 2);
        const unsigned complete = value ^ (select * 17);
        const unsigned sparse = value ^ sparseMasks[sparseIndex];
#if defined(SWITCH_GRAPH)
        model.selector[0] = select;
        model.data[0] = value;
        model.eval();
        assert(model.complete[0] == complete);
        assert(model.sparse[0] == sparse);
#elif defined(SWITCH_RTL)
        model.selector = select;
        model.data = value;
        model.eval();
        assert(model.complete == complete);
        assert(model.sparse == sparse);
#else
        selector = select;
        data = value;
        ++_system_clock;
        assert(uint64_t(model.complete_out()) == complete);
        assert(uint64_t(model.sparse_out()) == sparse);
#endif
    }
    std::puts("exhaustive promoted/masked switch: 4096 oracle samples PASS");
}
