#include "SpecializedCombRoot.h"
#include "SpecializedCombRoot_optimized_combs.h"

long _system_clock = 0;

int main() {
    SpecializedCombRoot reference;
    SpecializedCombRoot optimized;
    reference._assign();
    optimized._assign();
    for (unsigned input = 0; input < 65536; ++input) {
        reference.input = input;
        optimized.input = input;
        reference._work(false);
        calc_all(optimized, false);
        reference._strobe();
        commit_optimized_regs(optimized);
        if (reference.scope_sum != optimized.scope_sum ||
            uint64_t(optimized.scope_sum) != 23 ||
            reference.positive.scope_before != optimized.positive.scope_before ||
            uint64_t(optimized.positive.scope_before) != 9 ||
            reference.positive.scope_inside != optimized.positive.scope_inside ||
            uint64_t(optimized.positive.scope_inside) != 12 ||
            reference.positive.scope_after != optimized.positive.scope_after ||
            uint64_t(optimized.positive.scope_after) != 9 ||
            reference.positive.input_mix != optimized.positive.input_mix ||
            uint64_t(optimized.positive.input_mix) != ((input + 2) & 255) ||
            reference.positive.scope_strobe != optimized.positive.scope_strobe ||
            uint64_t(optimized.positive.scope_strobe) != 22) return 2;
        if (reference.observed_true != optimized.observed_true ||
            reference.observed_false != optimized.observed_false ||
            reference.observed_scope != optimized.observed_scope ||
            uint64_t(optimized.observed_scope) != ((input + 1) & 255)) return 1;
        ++_system_clock;
    }
}
