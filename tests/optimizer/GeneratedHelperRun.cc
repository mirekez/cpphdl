#include "GeneratedHelperRoot.h"
#include "GeneratedHelperRoot_optimized_combs.h"
#include <cstdio>

long _system_clock = 0;

static uint64_t expected(uint64_t value, uint64_t mask) {
    uint64_t local = 7;
    uint64_t total = 0;
    for (unsigned index = 0; index < 4; ++index) {
        local += index;
        total = (total + (value ^ local)) & mask;
    }
    return (total + local) & mask;
}

int main() {
    GeneratedHelperRoot model;
    model._assign();
    for (unsigned value = 0; value < 65536; ++value) {
        model.narrow.input = value;
        model.wide.input = uint64_t(value) * 65537;
        calc_all(model, false);
        if (uint64_t(model.narrow.observed) != expected((value + 3) & 65535, 65535) ||
            uint64_t(model.wide.observed) != expected((uint64_t(value) * 65537 + 9) & 0xffffffff, 0xffffffff)) {
            std::fprintf(stderr, "generated helper mismatch: %u\n", value);
            return 1;
        }
        ++_system_clock;
    }
}
