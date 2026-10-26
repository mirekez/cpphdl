#include "WorkMutationRoot.h"
#include "WorkMutationRoot_optimized_combs.h"

#include <cstdio>

long _system_clock = 0;

int main()
{
    WorkMutationRoot reference;
    reference._assign();
    reference._work(false);
    WorkMutationRoot root;
    root._assign();
    calc_all(root, false);
    if ((uint64_t)(root.leaf.observed) != 7 ||
        root.before != reference.before || root.after != reference.after ||
        (uint64_t)(root.before) != 1 || (uint64_t)(root.after) != 7) {
        std::fprintf(stderr, "observed=%llu before=%llu after=%llu\n",
                     (unsigned long long)(uint64_t)(root.leaf.observed),
                     (unsigned long long)(uint64_t)(root.before),
                     (unsigned long long)(uint64_t)(root.after));
        return 1;
    }
    return 0;
}
