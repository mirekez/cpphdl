#include "TemplateHelperRoot.h"
#include "TemplateHelperRoot_optimized_combs.h"
#include <cstdio>

long _system_clock = 0;

int main() {
    TemplateHelperRoot model;
    model._assign();
    for (unsigned value = 0; value < 65536; ++value) {
        model.input = value;
        calc_all(model, false);
        if (uint64_t(model.observed) != ((value + 3) & 65535)) {
            std::fprintf(stderr, "template helper mismatch: %u\n", value);
            return 1;
        }
        ++_system_clock;
    }
}
