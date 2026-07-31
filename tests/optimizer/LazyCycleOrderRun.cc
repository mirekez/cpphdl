#include "LazyCycleOrderRoot.h"
#include "LazyCycleOrderRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    LazyCycleOrderRoot root;
    root._assign();
    calc_all(root, false);
    calc_all(root, false);
    return (uint64_t)(root.work_value) == 1 &&
                   (uint64_t)(root.work_output) == 0 &&
                   root.evaluations == 1 && root.child.evaluations == 1
               ? 0
               : 1;
}
