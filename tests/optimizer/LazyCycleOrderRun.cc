#include "LazyCycleOrderRoot.h"
#include "LazyCycleOrderRoot_optimized_combs.h"

long _system_clock = 0;

int main()
{
    LazyCycleOrderRoot root;
    cpphdl::logic<1> inactive = 0;
    root.inactive_input = _ASSIGN_REG(inactive);
    root._assign();
    root.use_inactive_input = 1;
    calc_all(root, false);
    root.use_inactive_input = 0;
    ++_system_clock;
    calc_all(root, false);
    return (uint64_t)(root.work_value) == 0 &&
                   (uint64_t)(root.work_output) == 1 &&
                   root.evaluations == 4 && root.child.evaluations == 2
               ? 0
               : 1;
}
