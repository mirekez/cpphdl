#include "ProjectedHandshakeRoot.h"
#include "ProjectedHandshakeRoot_optimized_combs.h"
#include <cstdio>

long _system_clock = 0;

int main()
{
    ProjectedHandshakeRoot reference;
    ProjectedHandshakeRoot optimized;
    cpphdl::logic<1> input = 1;
    reference.input = _ASSIGN(input);
    optimized.input = _ASSIGN(input);
    reference._assign();
    optimized._assign();
    bind_optimized_ports(optimized);
    for (unsigned cycle = 0; cycle < 16; ++cycle, ++_system_clock) {
        input = (cycle & 1) ^ 1;
        const auto expectedValid = uint64_t(reference.valid());
        const auto expectedReady = uint64_t(reference.ready());
        calc_all(optimized, false);
        if (uint64_t(optimized.valid()) != expectedValid ||
            uint64_t(optimized.ready()) != expectedReady) {
            std::printf("cycle=%u valid=%llu/%llu ready=%llu/%llu\n", cycle,
                        (unsigned long long)uint64_t(optimized.valid()),
                        (unsigned long long)expectedValid,
                        (unsigned long long)uint64_t(optimized.ready()),
                        (unsigned long long)expectedReady);
            return 1;
        }
    }
}
