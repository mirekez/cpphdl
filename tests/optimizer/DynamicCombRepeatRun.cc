#include "DynamicCombRepeatRoot.h"
#include "DynamicCombRepeatRoot_optimized_combs.h"

#include <cstdio>

long _system_clock = 0;

int main()
{
    DynamicCombRepeatRoot root;
    root._assign();
    calc_all(root, false);
    const bool passed = (uint64_t)(root.work_output) == 0 &&
                        (uint64_t)(root.first_repeat) == 3 &&
                        (uint64_t)(root.second_repeat) == 7 &&
                        (uint64_t)(root.third_repeat) == 7 &&
                        (uint64_t)(root.fourth_repeat) == 7 &&
                        (uint64_t)(root.fifth_repeat) == 7 &&
                        (uint64_t)(root.sixth_repeat) == 7 &&
                        (uint64_t)(root.seventh_repeat) == 7 &&
                        (uint64_t)(root.first_lazy) == 5 &&
                        (uint64_t)(root.second_lazy) == 5 &&
                        root.lazy_evaluations == 1;
    if (!passed) {
        std::fprintf(stderr,
                     "work=%llu repeat=%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                     "lazy=%llu/%llu evaluations=%u\n",
                     (unsigned long long)(uint64_t)(root.work_output),
                     (unsigned long long)(uint64_t)(root.first_repeat),
                     (unsigned long long)(uint64_t)(root.second_repeat),
                     (unsigned long long)(uint64_t)(root.third_repeat),
                     (unsigned long long)(uint64_t)(root.fourth_repeat),
                     (unsigned long long)(uint64_t)(root.fifth_repeat),
                     (unsigned long long)(uint64_t)(root.sixth_repeat),
                     (unsigned long long)(uint64_t)(root.seventh_repeat),
                     (unsigned long long)(uint64_t)(root.first_lazy),
                     (unsigned long long)(uint64_t)(root.second_lazy),
                     root.lazy_evaluations);
    }
    return passed ? 0 : 1;
}
