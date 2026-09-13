#include "NativeBitStoreRoot.h"
#include "NativeBitStoreRoot_optimized_combs.h"

#include <cstdio>

long _system_clock = 0;

int main()
{
    NativeBitStoreRoot reference;
    NativeBitStoreRoot optimized;
    reference._assign();
    optimized._assign();
    for (unsigned input = 0; input < 65536; ++input) {
        reference.input = input;
        optimized.input = input;
        reference._work(false);
        calc_all(optimized, false);
        if (reference.observed != optimized.observed ||
            reference.wide_observed != optimized.wide_observed ||
            reference.array_observed[0] != optimized.array_observed[0] ||
            reference.array_observed[1] != optimized.array_observed[1]) {
            std::fprintf(stderr, "native store mismatch at input %u\n", input);
            return 1;
        }
        ++_system_clock;
    }
    return 0;
}
