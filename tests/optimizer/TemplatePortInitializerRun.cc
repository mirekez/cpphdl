#include "TemplatePortInitializer.h"
#include "TemplatePortInitializer_optimized_combs.h"
#include <cstdio>

long _system_clock = 0;

int main() {
    TemplatePortInitializer reference;
    TemplatePortInitializer optimized;
    reference._assign();
    optimized._assign();
    for (unsigned value = 13; value < 1024; ++value) {
        reference.wide.value = optimized.wide.value = value;
        reference.narrow.value = optimized.narrow.value = value;
        reference._work(false);
        calc_all(optimized, false);
        if (reference.observed != optimized.observed ||
            reference.truncated != optimized.truncated ||
            uint64_t(optimized.observed) != value ||
            uint64_t(optimized.truncated) != (value & 255)) {
            std::fprintf(stderr, "initialized template port mismatch at %u\n", value);
            return 1;
        }
        ++_system_clock;
    }
}
