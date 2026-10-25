#include "AssignInitialization.h"
#ifdef ASSIGN_OPTIMIZED
#include "AssignInitialization_optimized_combs.h"
#endif
#include <cassert>
#include <cstdio>

long _system_clock = 0;

int main() {
    AssignInitialization model;
    assert(static_cast<unsigned>(model.value) == 99);
    for (unsigned setup = 1; setup <= 2; ++setup) {
        ++_system_clock;
        model.value = cpphdl::logic<32>(71);
        model._assign();
#ifdef ASSIGN_OPTIMIZED
        bind_optimized_ports(model);
#endif
        assert(static_cast<unsigned>(model.value) == 23);
        assert(static_cast<unsigned>(model.before_child) == 17);
        assert(static_cast<unsigned>(model.after_child) == 17);
        assert(static_cast<unsigned>(model.leaf.initial_value) == 17);
        assert(static_cast<unsigned>(model.leaf.initialized) == 5);
        assert(model.assignments == setup && model.leaf.assignments == setup);
        for (unsigned cycle = 0; cycle < 5; ++cycle) {
            ++_system_clock;
            model.value = cpphdl::logic<32>(31 + cycle);
#ifdef ASSIGN_OPTIMIZED
            calc_all(model, false);
            commit_optimized_regs(model);
#else
            model._work(false);
            model._strobe();
#endif
            assert(static_cast<unsigned>(model.output()) == 48 + cycle);
            assert(static_cast<unsigned>(model.leaf.initialized) == 5);
            assert(model.assignments == setup && model.leaf.assignments == setup);
        }
    }
    std::puts("PASS: initialization order and execution count preserved");
}
