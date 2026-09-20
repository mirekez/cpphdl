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
        if (reference.observed_true != optimized.observed_true ||
            reference.observed_false != optimized.observed_false ||
            reference.observed_scope != optimized.observed_scope ||
            uint64_t(optimized.observed_scope) != ((input + 1) & 255)) return 1;
        ++_system_clock;
    }
}
