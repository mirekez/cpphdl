#include "IndexedProofRoot.h"
#include "IndexedProofRoot_optimized_combs.h"
#include <cstdio>

long _system_clock = 0;

int main()
{
    IndexedProofRoot reference, optimized;
    reference.bank[0]._next = 0x5a;
    optimized.bank[0]._next = 0x5a;
    reference.child.value._next = 5;
    optimized.child.value._next = 5;
    reference._strobe();
    optimized._strobe();
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        ++_system_clock;
        reference._work(false);
        calc_all(optimized);
        if (reference.before != optimized.before || reference.after != optimized.after ||
            reference.child.observed != optimized.child.observed ||
            reference.stableBefore != optimized.stableBefore ||
            reference.stableAfter != optimized.stableAfter ||
            optimized.stableBefore != optimized.stableAfter ||
            optimized.before == optimized.after) return 1;
        reference._strobe();
        commit_optimized_regs(optimized);
    }
    std::puts("indexed proof: mutable work and register-next phases passed");
}
