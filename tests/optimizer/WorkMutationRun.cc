#include "WorkMutationRoot.h"
#include "WorkMutationRoot_optimized_combs.h"

#include <cstdio>

long _system_clock = 0;

int main()
{
    WorkMutationRoot root;
    root._assign();
    calc_all(root, false);
    if ((uint64_t)(root.leaf.observed) != 7) {
        std::fprintf(stderr, "observed=%llu\n",
                     (unsigned long long)(uint64_t)(root.leaf.observed));
        return 1;
    }
    return 0;
}
